/*
 * Stellarium
 * Copyright (C) 2008 Fabien Chereau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#include "StelPainter.hpp"

#include "StelApp.hpp"
#include "StelMainView.hpp"
#include "StelLocaleMgr.hpp"
#include "StelProjector.hpp"
#include "StelProjectorClasses.hpp"
#include "StelUtils.hpp"
#include "StelTextureMgr.hpp"
#include "SaturationShader.hpp"

#include <QDebug>
#include <QString>
#include <QSettings>
#include <QPainter>
#include <QMutex>
#include <QVarLengthArray>
#include <QPaintEngine>
#include <QCache>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLPaintDevice>
#include <QOpenGLBuffer>
#include <QOpenGLShader>
#include <QOpenGLTexture>
#include <QApplication>

namespace
{
static const int TEX_CACHE_LIMIT = 7000000;

// These raw pointers are never deleted, because the only time their pointees
// need to be destroyed is when the whole program is exiting, and at this point
// we may not even have an OpenGL context anymore, so we'll never need to
// delete these objects manually.
QOpenGLVertexArrayObject* vao;
QOpenGLBuffer* verticesVBO;
QOpenGLBuffer* indicesVBO;
}

#ifndef NDEBUG
QMutex* StelPainter::globalMutex = new QMutex();
#endif

QCache<QByteArray, StringTexture> StelPainter::texCache(TEX_CACHE_LIMIT);
QOpenGLShaderProgram* StelPainter::texturesShaderProgram=Q_NULLPTR;
QOpenGLShaderProgram* StelPainter::textShaderProgram=Q_NULLPTR;
QOpenGLShaderProgram* StelPainter::basicShaderProgram=Q_NULLPTR;
QOpenGLShaderProgram* StelPainter::colorShaderProgram=Q_NULLPTR;
QOpenGLShaderProgram* StelPainter::texturesColorShaderProgram=Q_NULLPTR;
QOpenGLShaderProgram* StelPainter::wideLineShaderProgram=Q_NULLPTR;
QOpenGLShaderProgram* StelPainter::colorfulWideLineShaderProgram=Q_NULLPTR;
StelPainter::BasicShaderVars StelPainter::basicShaderVars;
StelPainter::TexturesShaderVars StelPainter::texturesShaderVars;
StelPainter::TextShaderVars StelPainter::textShaderVars;
StelPainter::BasicShaderVars StelPainter::colorShaderVars;
StelPainter::TexturesColorShaderVars StelPainter::texturesColorShaderVars;
StelPainter::WideLineShaderVars StelPainter::wideLineShaderVars;
StelPainter::ColorfulWideLineShaderVars StelPainter::colorfulWideLineShaderVars;
bool StelPainter::multisamplingEnabled=false;

StelPainter::GLState::GLState(QOpenGLFunctions* gl)
	: blend(false),
	  blendSrc(GL_SRC_ALPHA), blendDst(GL_ONE_MINUS_SRC_ALPHA),
	  depthTest(false),
	  depthMask(false),
	  cullFace(false),
	  lineSmooth(false),
	  lineWidth(1.0f),
	  gl(gl)
{
}

void StelPainter::GLState::apply()
{
	if(blend)
		gl->glEnable(GL_BLEND);
	else
		gl->glDisable(GL_BLEND);
	gl->glBlendFunc(blendSrc,blendDst);
	if(depthTest)
		gl->glEnable(GL_DEPTH_TEST);
	else
		gl->glDisable(GL_DEPTH_TEST);
	gl->glDepthMask(depthMask);
	if(cullFace)
		gl->glEnable(GL_CULL_FACE);
	else
		gl->glDisable(GL_CULL_FACE);
	gl->glLineWidth(lineWidth);
#ifdef GL_LINE_SMOOTH
	if(!QOpenGLContext::currentContext()->isOpenGLES())
	{
		if (lineSmooth)
			gl->glEnable(GL_LINE_SMOOTH);
		else
			gl->glDisable(GL_LINE_SMOOTH);
	}
#endif
}

void StelPainter::GLState::reset()
{
	*this = GLState(gl);
	apply();
}

bool StelPainter::linkProg(QOpenGLShaderProgram* prog, const QString& name)
{
	bool ret = prog->link();
	QString log = prog->log();
	if (!ret || (!log.isEmpty() && !log.contains("Link was successful") && !(log=="No errors."))) //"No errors." returned on some Intel drivers
		qWarning().noquote() << QString("StelPainter: Warnings while linking %1 shader program:\n%2").arg(name, prog->log());
	return ret;
}

StelPainter::StelPainter(const StelProjectorP& proj)
	: QOpenGLFunctions(QOpenGLContext::currentContext())
	, glState(this)
{
	Q_ASSERT(proj);

#ifndef NDEBUG
	Q_ASSERT(globalMutex);
	
	GLenum er = glGetError();
	if (er!=GL_NO_ERROR)
	{
		if (er==GL_INVALID_OPERATION)
			qFatal("Invalid openGL operation. It is likely that you used openGL calls without having a valid instance of StelPainter");
	}

	// Lock the global mutex ensuring that no other instances of StelPainter are currently being used
	if (globalMutex->tryLock()==false)
	{
		qFatal("There can be only 1 instance of StelPainter at a given time");
	}
#endif

	//TODO: is this still required, and is there some Qt way to fix it? 0x11111111 is a bit peculiar, how was it chosen?
	// Fix some problem when using Qt OpenGL2 engine
	glStencilMask(0x11111111);
	glState.apply(); //apply default OpenGL state
	setProjector(proj);

	if(!vao)
	{
		// First ever creation of StelPainter, initialize the objects
		vao = new QOpenGLVertexArrayObject;
		verticesVBO = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
		indicesVBO = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);

		vao->create();
		indicesVBO->create();
		indicesVBO->setUsagePattern(QOpenGLBuffer::StreamDraw);
		verticesVBO->create();
		verticesVBO->setUsagePattern(QOpenGLBuffer::StreamDraw);
	}
}

void StelPainter::setProjector(const StelProjectorP& p)
{
	prj=p;
	// Init GL viewport to current projector values
	glViewport(prj->viewportXywh[0], prj->viewportXywh[1], prj->viewportXywh[2], prj->viewportXywh[3]);
	glFrontFace(prj->needGlFrontFaceCW()?GL_CW:GL_CCW);
}

StelPainter::~StelPainter()
{
	//reset opengl state
	glState.reset();

#ifndef NDEBUG
	GLenum er = glGetError();
	if (er!=GL_NO_ERROR)
	{
		if (er==GL_INVALID_OPERATION)
			qFatal("Invalid openGL operation detected in ~StelPainter()");
	}

	// We are done with this StelPainter
	globalMutex->unlock();
#endif
}


void StelPainter::setFont(const QFont& font)
{
	currentFont = font;
}

void StelPainter::setColor(float r, float g, float b, float a)
{
	currentColor.set(r,g,b,a);
}

void StelPainter::setColor(Vec3f rgb, float a)
{
	currentColor.set(rgb[0],rgb[1],rgb[2],a);
}

void StelPainter::setColor(Vec4f rgba)
{
	currentColor=rgba;
}

Vec4f StelPainter::getColor() const
{
	return currentColor;
}

QFontMetrics StelPainter::getFontMetrics() const
{
	return QFontMetrics(currentFont);
}

void StelPainter::setBlending(bool enableBlending, GLenum blendSrc, GLenum blendDst)
{
	if(enableBlending != glState.blend)
	{
		glState.blend = enableBlending;
		if(enableBlending)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
	}
	if(enableBlending)
	{
		if(blendSrc!=glState.blendSrc||blendDst!=glState.blendDst)
		{
			glState.blendSrc = blendSrc;
			glState.blendDst = blendDst;
			glBlendFunc(blendSrc,blendDst);
		}
	}
}

bool StelPainter::getBlending(GLenum *src, GLenum *dst) const
{
	if (dst!=Q_NULLPTR)
		*dst=glState.blendDst;
	if (src!=Q_NULLPTR)
		*src=glState.blendSrc;
	return glState.blend;
}

void StelPainter::setDepthTest(bool enable)
{
	if(glState.depthTest != enable)
	{
		glState.depthTest = enable;
		if(enable)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
	}
}

void StelPainter::setDepthMask(bool enable)
{
	if(glState.depthMask != enable)
	{
		glState.depthMask = enable;
		if(enable)
			glDepthMask(GL_TRUE);
		else
			glDepthMask(GL_FALSE);
	}
}

void StelPainter::setCullFace(bool enable)
{
	if(glState.cullFace!=enable)
	{
		glState.cullFace = enable;
		if(enable)
			glEnable(GL_CULL_FACE);
		else
			glDisable(GL_CULL_FACE);
	}
}

void StelPainter::setLineSmooth(bool enable)
{
#ifdef GL_LINE_SMOOTH
	if (!QOpenGLContext::currentContext()->isOpenGLES() && enable!=glState.lineSmooth)
	{
		glState.lineSmooth = enable;
		if(enable)
			glEnable(GL_LINE_SMOOTH);
		else
			glDisable(GL_LINE_SMOOTH);
	}
#else
	Q_UNUSED(enable); //noop
#endif
}

void StelPainter::setLineWidth(float width)
{
	if(fabs(glState.lineWidth - width) < 1.e-10f)
		return;

	glState.lineWidth = width;

	if(width > 1 && StelMainView::getInstance().getGLInformation().isCoreProfile)
		return;

	glLineWidth(width);
}

///////////////////////////////////////////////////////////////////////////
// Standard methods for drawing primitives

// Fill with black around the circle
void StelPainter::drawViewportShape(void)
{
	if (prj->maskType != StelProjector::MaskDisk)
		return;

	bool oldBlendState = glState.blend;
	glDisable(GL_BLEND);
	setColor(0.f,0.f,0.f);

	GLfloat innerRadius = 0.5f*static_cast<float>(prj->viewportFovDiameter);
	GLfloat outerRadius = static_cast<float>(prj->getViewportWidth()+prj->getViewportHeight());
	GLint slices = 239;

	GLfloat sinCache[240];
	GLfloat cosCache[240];
	GLfloat vertices[(240+1)*2][3];
	GLfloat deltaRadius;
	GLfloat radiusHigh;

	if (outerRadius<=0.0f || innerRadius<0.0f ||innerRadius > outerRadius)
	{
		Q_ASSERT(0);
		return;
	}

	/* Compute length (needed for normal calculations) */
	deltaRadius=outerRadius-innerRadius;

	/* Cache is the vertex locations cache */
	for (int i=0; i<=slices; i++)
	{
		GLfloat angle=(M_PIf*2.0f)*static_cast<float>(i)/static_cast<float>(slices);
		sinCache[i]=static_cast<GLfloat>(sin(angle));
		cosCache[i]=static_cast<GLfloat>(cos(angle));
	}

	sinCache[slices]=sinCache[0];
	cosCache[slices]=cosCache[0];

	/* Enable arrays */
	enableClientStates(true);
	setVertexPointer(3, GL_FLOAT, vertices);

	radiusHigh=outerRadius-deltaRadius;
	for (int i=0; i<=slices; i++)
	{
		vertices[i*2][0]= static_cast<float>(prj->viewportCenter[0]) + outerRadius*sinCache[i];
		vertices[i*2][1]= static_cast<float>(prj->viewportCenter[1]) + outerRadius*cosCache[i];
		vertices[i*2][2] = 0.0f;
		vertices[i*2+1][0]= static_cast<float>(prj->viewportCenter[0]) + radiusHigh*sinCache[i];
		vertices[i*2+1][1]= static_cast<float>(prj->viewportCenter[1]) + radiusHigh*cosCache[i];
		vertices[i*2+1][2] = 0.0f;
	}
	drawFromArray(TriangleStrip, (slices+1)*2, 0, false);
	enableClientStates(false);
	if(oldBlendState)
		glEnable(GL_BLEND);
}

void StelPainter::computeFanDisk(float radius, uint innerFanSlices, uint level, QVector<Vec3d>& vertexArr, QVector<Vec2f>& texCoordArr)
{
	Q_ASSERT(level<32);
	float rad[64];
	uint i,j;
	rad[level] = radius;
#pragma warning(suppress: 4146)
	for (i=level-1u;i!=-1u;--i)
	{
		rad[i] = rad[i+1]*(1.f-M_PIf/(innerFanSlices<<(i+1)))*2.f/3.f;
	}
	uint slices = innerFanSlices<<level;

	float* cos_sin_theta = StelUtils::ComputeCosSinTheta(static_cast<uint>(slices));
	float* cos_sin_theta_p;
	uint slices_step = 2;
	float x,y,xa,ya;
	radius*=2.f;
	vertexArr.resize(0);
	texCoordArr.resize(0);
	for (i=level;i>0;--i,slices_step<<=1)
	{
		for (j=0,cos_sin_theta_p=cos_sin_theta; j<slices-1; j+=slices_step,cos_sin_theta_p+=2*slices_step)
		{
			xa = rad[i]*cos_sin_theta_p[slices_step];
			ya = rad[i]*cos_sin_theta_p[slices_step+1];
			texCoordArr << Vec2f(0.5f+xa/radius, 0.5f+ya/radius);
			vertexArr << Vec3d(static_cast<double>(xa), static_cast<double>(ya), 0);

			x = rad[i]*cos_sin_theta_p[2*slices_step];
			y = rad[i]*cos_sin_theta_p[2*slices_step+1];
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);

			x = rad[i-1]*cos_sin_theta_p[2*slices_step];
			y = rad[i-1]*cos_sin_theta_p[2*slices_step+1];
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);

			texCoordArr << Vec2f(0.5f+xa/radius, 0.5f+ya/radius);
			vertexArr << Vec3d(static_cast<double>(xa), static_cast<double>(ya), 0);
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);

			x = rad[i-1]*cos_sin_theta_p[0];
			y = rad[i-1]*cos_sin_theta_p[1];
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);

			texCoordArr << Vec2f(0.5f+xa/radius, 0.5f+ya/radius);
			vertexArr << Vec3d(static_cast<double>(xa), static_cast<double>(ya), 0);
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);

			x = rad[i]*cos_sin_theta_p[0];
			y = rad[i]*cos_sin_theta_p[1];
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);
		}
	}
	// draw the inner polygon
	slices_step>>=1;
	cos_sin_theta_p=cos_sin_theta;

	if (slices==1)
	{
		x = rad[0]*cos_sin_theta_p[0];
		y = rad[0]*cos_sin_theta_p[1];
		texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
		vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);
		cos_sin_theta_p+=2*slices_step;
		x = rad[0]*cos_sin_theta_p[0];
		y = rad[0]*cos_sin_theta_p[1];
		texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
		vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);
		cos_sin_theta_p+=2*slices_step;
		x = rad[0]*cos_sin_theta_p[0];
		y = rad[0]*cos_sin_theta_p[1];
		texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
		vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);
	}
	else
	{
		j=0;
		while (j<slices)
		{
			texCoordArr << Vec2f(0.5f, 0.5f);
			vertexArr << Vec3d(0, 0, 0);
			x = rad[0]*cos_sin_theta_p[0];
			y = rad[0]*cos_sin_theta_p[1];
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);
			j+=slices_step;
			cos_sin_theta_p+=2*slices_step;
			x = rad[0]*cos_sin_theta_p[0];
			y = rad[0]*cos_sin_theta_p[1];
			texCoordArr << Vec2f(0.5f+x/radius, 0.5f+y/radius);
			vertexArr << Vec3d(static_cast<double>(x), static_cast<double>(y), 0);
		}
	}
}

static void sSphereMapTexCoordFast(float rho_div_fov, const float costheta, const float sintheta, QVector<float>& out)
{
	if (rho_div_fov>0.5f)
		rho_div_fov=0.5f;
	out << 0.5f + rho_div_fov * costheta << 0.5f + rho_div_fov * sintheta;
}

void StelPainter::sSphereMap(double radius, unsigned int slices, unsigned int stacks, float textureFov, int orientInside)
{
	float rho;
	double x,y,z;
	unsigned int i, j;
	const float* cos_sin_rho = StelUtils::ComputeCosSinRho(stacks);
	const float* cos_sin_rho_p;

	const float* cos_sin_theta = StelUtils::ComputeCosSinTheta(slices);
	const float* cos_sin_theta_p;

	float drho = M_PIf / static_cast<float>(stacks);
	drho/=textureFov;

	// texturing: s goes from 0.0/0.25/0.5/0.75/1.0 at +y/+x/-y/-x/+y axis
	// t goes from -1.0/+1.0 at z = -radius/+radius (linear along longitudes)
	// cannot use triangle fan on texturing (s coord. at top/bottom tip varies)

	const unsigned int imax = stacks;

	static QVector<double> vertexArr;
	static QVector<float> texCoordArr;

	// draw intermediate stacks as quad strips
	// LGTM comments: the floats are always <=1. We still prefer float multiplication (with insignificant accuracy loss) for speed.
	if (!orientInside) // nsign==1
	{
		for (i = 0,cos_sin_rho_p=cos_sin_rho,rho=0.f; i < imax; ++i,cos_sin_rho_p+=2,rho+=drho)
		{
			vertexArr.resize(0);
			texCoordArr.resize(0);
			for (j=0,cos_sin_theta_p=cos_sin_theta;j<=slices;++j,cos_sin_theta_p+=2)
			{
				x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[1]);
				y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[1]);
				z = static_cast<double>(cos_sin_rho_p[0]);
				sSphereMapTexCoordFast(rho, cos_sin_theta_p[0], cos_sin_theta_p[1], texCoordArr);
				vertexArr << x*radius << y*radius << z*radius;

				x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[3]);
				y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[3]);
				z = static_cast<double>(cos_sin_rho_p[2]);
				sSphereMapTexCoordFast(rho + drho, cos_sin_theta_p[0], cos_sin_theta_p[1], texCoordArr);
				vertexArr << x*radius << y*radius << z*radius;
			}
			setArrays(reinterpret_cast<const Vec3d*>(vertexArr.constData()), reinterpret_cast<const Vec2f*>(texCoordArr.constData()));
			drawFromArray(TriangleStrip, vertexArr.size()/3);
		}
	}
	else
	{
		for (i = 0,cos_sin_rho_p=cos_sin_rho,rho=0.f; i < imax; ++i,cos_sin_rho_p+=2,rho+=drho)
		{
			vertexArr.resize(0);
			texCoordArr.resize(0);
			for (j=0,cos_sin_theta_p=cos_sin_theta;j<=slices;++j,cos_sin_theta_p+=2)
			{
				x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[3]);
				y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[3]);
				z = static_cast<double>(cos_sin_rho_p[2]);
				sSphereMapTexCoordFast(rho + drho, cos_sin_theta_p[0], -cos_sin_theta_p[1], texCoordArr);
				vertexArr << x*radius << y*radius << z*radius;

				x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[1]);
				y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[1]);
				z = static_cast<double>(cos_sin_rho_p[0]);
				sSphereMapTexCoordFast(rho, cos_sin_theta_p[0], -cos_sin_theta_p[1], texCoordArr);
				vertexArr << x*radius << y*radius << z*radius;
			}
			setArrays(reinterpret_cast<const Vec3d*>(vertexArr.constData()), reinterpret_cast<const Vec2f*>(texCoordArr.constData()));
			drawFromArray(TriangleStrip, vertexArr.size()/3);
		}
	}
}

void StelPainter::drawTextGravity180(const float x, const float y, const QString& ws, const float xshift, const float yshift)
{
	const float dx = x - static_cast<float>(prj->viewportCenter[0]);
	const float dy = y - static_cast<float>(prj->viewportCenter[1]);
	float d = std::sqrt(dx*dx + dy*dy);

	// If the text is too far away to be visible in the screen return
	if (d>qMax(prj->viewportXywh[3], prj->viewportXywh[2])*2 || ws.isEmpty())
		return;

	const auto fm = getFontMetrics();
	const bool rtl = StelApp::getInstance().getLocaleMgr().isSkyRTL();

	const float ppx = static_cast<float>(prj->getDevicePixelsPerPixel());
	float anglePerUnitWidth = ppx / d;
	float theta = std::atan2(dy - 1, dx);

	float xVc = static_cast<float>(prj->viewportCenter[0]) + xshift;
	float yVc = static_cast<float>(prj->viewportCenter[1]) + yshift;

	const float charWidth = fm.averageCharWidth();
	constexpr float maxAnglePerChar = 10 * M_PI_180f;
	if (charWidth * anglePerUnitWidth > maxAnglePerChar)
	{
		// Too curvy text, limit its curvature by moving the center of curvature away
		const float x0 = d * std::cos(theta) + xVc;
		const float y0 = d * std::sin(theta) + yVc;

		anglePerUnitWidth = maxAnglePerChar / charWidth;
		d = ppx / anglePerUnitWidth;

		xVc = x0 - d * std::cos(theta);
		yVc = y0 - d * std::sin(theta);
	}

	const int slen = ws.length();
	const int startI = rtl ? slen - 1 : 0;
	const int endI = rtl ? -1 : slen;
	const int inc = rtl ? -1 : 1;
	for (int i = startI; i != endI; i += inc)
	{
		const QChar c = ws[i];
		const float x = d * std::cos(theta) + xVc;
		const float y = d * std::sin(theta) + yVc;
		drawText(x, y, c, 90.f + theta*M_180_PIf, 0., 0.);
		theta += fm.horizontalAdvance(c) * anglePerUnitWidth;
	}
}

void StelPainter::drawText(const Vec3d& v, const QString& str, float angleDeg, float xshift, float yshift, bool noGravity)
{
	Vec3d win;
	if (prj->project(v, win))
		drawText(static_cast<float>(win[0]), static_cast<float>(win[1]), str, angleDeg, xshift, yshift, noGravity);
}

/*************************************************************************
 Draw the string at the given position and angle with the given font
*************************************************************************/

// Methods taken from text-use-opengl-buffer
// Container for one cached string texture
struct StringTexture
{
	QOpenGLTexture* texture;
	QSize size;
	QPoint baselineShift;
	QSizeF getTexSize() const {
		return QSizeF(static_cast<qreal>(size.width())  / static_cast<qreal>(texture->width()),
			      static_cast<qreal>(size.height()) / static_cast<qreal>(texture->height()));
	}

	StringTexture(QOpenGLTexture* tex, const QSize& size, const QPoint& baselineShift) :
	     texture(tex), size(size), baselineShift(baselineShift) {}
	~StringTexture() {delete texture;}
};

StringTexture* StelPainter::getTextTexture(const QString& str, int pixelSize) const
{
	QByteArray hash = str.toUtf8() + QByteArray::number(pixelSize);
	StringTexture* cachedTex = texCache.object(hash);
	if (cachedTex)
		return cachedTex;
	QFont tmpFont = currentFont;
	tmpFont.setPixelSize(currentFont.pixelSize()*prj->getDevicePixelsPerPixel());
	tmpFont.setStyleStrategy(QFont::NoSubpixelAntialias); // The text may rotate, which would break subpixel AA
	QRect strRect = QFontMetrics(tmpFont).boundingRect(str);
	int w = strRect.width()+1+static_cast<int>(0.02f*strRect.width());
	int h = strRect.height();

	QImage strImage(StelUtils::getBiggerPowerOfTwo(w), StelUtils::getBiggerPowerOfTwo(h),
	                QImage::Format_RGBX8888);
	strImage.fill(Qt::black);
	QPainter painter(&strImage);
	painter.setRenderHints(QPainter::TextAntialiasing);
	painter.setFont(tmpFont);
	painter.setPen(Qt::white);
	painter.drawText(-strRect.x(), -strRect.y(), str);
	StringTexture* newTex = new StringTexture(new QOpenGLTexture(strImage), QSize(w, h), QPoint(strRect.x(), -(strRect.y()+h)));
	newTex->texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
	texCache.insert(hash, newTex, static_cast<long>(w)*h*3);
	// simply returning newTex is dangerous as the object is owned by the cache now. (Coverity Scan barks.)
	return texCache.object(hash);
}

void StelPainter::drawText(float x, float y, const QString& str, float angleDeg, float xshift, float yshift, bool noGravity)
{
	if (prj->gravityLabels && !noGravity)
	{
		drawTextGravity180(x, y, str, xshift, yshift);
	}
	else
	{
		StringTexture* tex = getTextTexture(str, currentFont.pixelSize());
		Q_ASSERT(tex);
		if (!noGravity)
			angleDeg += prj->defaultAngleForGravityText;
		GLint oldTex = 0;
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
		tex->texture->bind(0);
		xshift += tex->baselineShift.x();
		yshift += tex->baselineShift.y();

		static float vertexData[8];
		// compute the vertex coordinates applying the translation and the rotation
		static const float vertexBase[] = {0., 0., 1., 0., 0., 1., 1., 1.};
		if (std::fabs(angleDeg)>1.f*M_PI_180f)
		{
			const float cosr = std::cos(angleDeg * M_PI_180f);
			const float sinr = std::sin(angleDeg * M_PI_180f);
			for (int i = 0; i < 8; i+=2)
			{
				vertexData[i]   = x + (tex->size.width()*vertexBase[i]+xshift) * cosr - (tex->size.height()*vertexBase[i+1]+yshift) * sinr;
				vertexData[i+1] = y + (tex->size.width()*vertexBase[i]+xshift) * sinr + (tex->size.height()*vertexBase[i+1]+yshift) * cosr;
			}
		}
		else
		{
			for (int i = 0; i < 8; i+=2)
			{
				vertexData[i]   = int(x + tex->size.width()*vertexBase[i]+xshift);
				vertexData[i+1] = int(y + tex->size.height()*vertexBase[i+1]+yshift);
			}
		}

		float texCoords[8];
		for (int i=0;i<4;i++)
		{
			texCoords[i*2+0] = static_cast<float>(tex->getTexSize().width()) * (i % 2);
			texCoords[i*2+1] = static_cast<float>(tex->getTexSize().height()) * (1 - i / 2);
		}
		setTexCoordPointer(2, GL_FLOAT, texCoords);

		//text drawing requires blending, but we reset GL state afterwards if necessary
		bool oldBlending = glState.blend;
		GLenum oldSrc = glState.blendSrc, oldDst = glState.blendDst;
		setBlending(true);
		enableClientStates(true, true);
		setVertexPointer(2, GL_FLOAT, vertexData);

		const Mat4f& m = getProjector()->getProjectionMatrix();
		const QMatrix4x4 qMat(m[0], m[4], m[8], m[12],
		                      m[1], m[5], m[9], m[13],
		                      m[2], m[6], m[10], m[14],
		                      m[3], m[7], m[11], m[15]);

		vao->bind();
		verticesVBO->bind();
		const GLsizeiptr vertexCount = 4;

		auto& pr = *textShaderProgram;
		pr.bind();

		const auto bufferSize = vertexArray.vertexSizeInBytes()*vertexCount +
		                        texCoordArray.vertexSizeInBytes()*vertexCount;
		verticesVBO->allocate(bufferSize);
		const auto vertexDataSize = vertexArray.vertexSizeInBytes()*vertexCount;
		verticesVBO->write(0, vertexArray.pointer, vertexDataSize);
		const auto texCoordDataOffset = vertexDataSize;
		const auto texCoordDataSize = texCoordArray.vertexSizeInBytes()*vertexCount;
		verticesVBO->write(texCoordDataOffset, texCoordArray.pointer, texCoordDataSize);

		pr.setAttributeBuffer(textShaderVars.vertex, vertexArray.type, 0, vertexArray.size);
		pr.enableAttributeArray(textShaderVars.vertex);
		pr.setUniformValue(textShaderVars.projectionMatrix, qMat);
		pr.setUniformValue(textShaderVars.textColor, currentColor.toQVector());
		pr.setAttributeBuffer(textShaderVars.texCoord, texCoordArray.type,
		                      texCoordDataOffset, texCoordArray.size);
		pr.enableAttributeArray(textShaderVars.texCoord);

		glDrawArrays(TriangleStrip, 0, vertexCount);

		verticesVBO->release();
		vao->release();

		pr.release();

		setBlending(oldBlending, oldSrc, oldDst);
		enableClientStates(false, false);
		glBindTexture(GL_TEXTURE_2D, oldTex);
	}
}

// Recursive method cutting a small circle in small segments
inline void fIter(const StelProjectorP& prj, const Vec3d& p1, const Vec3d& p2, Vec3d& win1, Vec3d& win2, std::list<Vec3d>& vertexList, const std::list<Vec3d>::const_iterator& iter, double radius, const Vec3d& center, int nbI=0, bool checkCrossDiscontinuity=true)
{
	const bool crossDiscontinuity = checkCrossDiscontinuity && prj->intersectViewportDiscontinuity(p1+center, p2+center);
	if (crossDiscontinuity && nbI>=10)
	{
		win1[2]=-2.;
		win2[2]=-2.;
		vertexList.insert(iter, win1);
		vertexList.insert(iter, win2);
		return;
	}

	Vec3d newVertex(p1); newVertex+=p2;
	newVertex.normalize();
	newVertex*=radius;
	Vec3d win3(newVertex[0]+center[0], newVertex[1]+center[1], newVertex[2]+center[2]);
	const bool isValidVertex = prj->projectInPlace(win3);

	const float v10=static_cast<float>(win1[0]-win3[0]);
	const float v11=static_cast<float>(win1[1]-win3[1]);
	const float v20=static_cast<float>(win2[0]-win3[0]);
	const float v21=static_cast<float>(win2[1]-win3[1]);

	const float dist = std::sqrt((v10*v10+v11*v11)*(v20*v20+v21*v21));
	const float cosAngle = (v10*v20+v11*v21)/dist;
	if ((cosAngle>-0.999f || dist>50*50 || crossDiscontinuity) && nbI<10)
	{
		// Use the 3rd component of the vector to store whether the vertex is valid
		win3[2]= isValidVertex ? 1.0 : -1.;
		fIter(prj, p1, newVertex, win1, win3, vertexList, vertexList.insert(iter, win3), radius, center, nbI+1, crossDiscontinuity || dist>50*50);
		fIter(prj, newVertex, p2, win3, win2, vertexList, iter, radius, center, nbI+1, crossDiscontinuity || dist>50*50 );
	}
}

// Used by the method below. A test before 24.3 showed no more than 64 vertices ever used.
// This uses much faster stack allocation if specified max length is not exceeded, else falls back to heap allocation.
QVarLengthArray<Vec3f, 128> StelPainter::smallCircleVertexArray;
QVarLengthArray<Vec4f, 128> StelPainter::smallCircleColorArray;

void StelPainter::drawSmallCircleVertexArray()
{
	if (smallCircleVertexArray.size() == 1)
	{
		smallCircleVertexArray.resize(0);
		smallCircleColorArray.resize(0);
		return;
	}
	if (smallCircleVertexArray.isEmpty())
		return;
	//if (smallCircleVertexArray.length()>64)
	//	qDebug() << "Size: " << smallCircleVertexArray.length();

	enableClientStates(true, false, !smallCircleColorArray.isEmpty());
	setVertexPointer(3, GL_FLOAT, smallCircleVertexArray.constData());
	if (!smallCircleColorArray.isEmpty())
		setColorPointer(4, GL_FLOAT, smallCircleColorArray.constData());
	drawFromArray(LineStrip, smallCircleVertexArray.size(), 0, false);
	enableClientStates(false);
	smallCircleVertexArray.resize(0);
	smallCircleColorArray.resize(0);
}

void StelPainter::drawGreatCircleArc(const Vec3d& start, const Vec3d& stop, const SphericalCap* clippingCap,
	void (*viewportEdgeIntersectCallback)(const Vec3d& screenPos, const Vec3d& direction, void* userData), void* userData)
 {
	 if (clippingCap)
	 {
		 Vec3d pt1=start;
		 Vec3d pt2=stop;
		 if (clippingCap->clipGreatCircle(pt1, pt2))
		 {
			drawSmallCircleArc(pt1, pt2, Vec3d(0.), viewportEdgeIntersectCallback, userData);
		 }
		 return;
	}
	drawSmallCircleArc(start, stop, Vec3d(0.), viewportEdgeIntersectCallback, userData);
 }

/*************************************************************************
 Draw a small circle arc in the current frame
*************************************************************************/
void StelPainter::drawSmallCircleArc(const Vec3d& start, const Vec3d& stop, const Vec3d& rotCenter, void (*viewportEdgeIntersectCallback)(const Vec3d& screenPos, const Vec3d& direction, void* userData), void* userData)
{
	Q_ASSERT(smallCircleVertexArray.empty());

	std::list<Vec3d> tessArc;	// Contains the list of projected points from the tesselated arc. (QLinkedList no longer available in Qt6.)
	Vec3d win1, win2;
	win1[2] = prj->project(start, win1) ? 1.0 : -1.;
	win2[2] = prj->project(stop, win2) ? 1.0 : -1.;
	tessArc.push_back(win1);


	if (rotCenter.normSquared()<1e-11)
	{
		// Great circle
		// Perform the tesselation of the arc in small segments in a way so that the lines look smooth
		fIter(prj, start, stop, win1, win2, tessArc, tessArc.insert(tessArc.end(), win2), 1, rotCenter);
	}
	else
	{
		Vec3d tmp = (rotCenter^start)/rotCenter.norm();
		const double radius = fabs(tmp.norm());
		// Perform the tesselation of the arc in small segments in a way so that the lines look smooth
		fIter(prj, start-rotCenter, stop-rotCenter, win1, win2, tessArc, tessArc.insert(tessArc.end(), win2), radius, rotCenter);
	}

	// And draw.
	std::list<Vec3d>::const_iterator i = tessArc.cbegin();
	//while (i<tessArc.cend())
	while (std::next(i, 1) != tessArc.cend())
	{
		const Vec3d& p1 = *i;
		const Vec3d& p2 = *(++i);
		const bool p1InViewport = prj->checkInViewport(p1);
		const bool p2InViewport = prj->checkInViewport(p2);
		if ((p1[2]>0 && p1InViewport) || (p2[2]>0 && p2InViewport))
		{
			smallCircleVertexArray.append(p1.toVec3f()); //Vec3f(static_cast<float>(p1[0]), static_cast<float>(p1[1]), static_cast<float>(p1[2])));
			if (std::next(i,1)==tessArc.cend())
			{
				smallCircleVertexArray.append(p2.toVec3f()); //Vec3f(static_cast<float>(p2[0]), static_cast<float>(p2[1]), static_cast<float>(p2[2])));
				drawSmallCircleVertexArray();
			}
			if (viewportEdgeIntersectCallback && p1InViewport!=p2InViewport)
			{
				// We crossed the edge of the view port
				if (p1InViewport)
					viewportEdgeIntersectCallback(prj->viewPortIntersect(p1, p2), p2-p1, userData);
				else
					viewportEdgeIntersectCallback(prj->viewPortIntersect(p2, p1), p1-p2, userData);
			}
		}
		else
		{
			// Break the line, draw the stored vertex and flush the list
			if (!smallCircleVertexArray.isEmpty())
				smallCircleVertexArray.append(Vec3f(static_cast<float>(p1[0]), static_cast<float>(p1[1]), static_cast<float>(p1[2])));
			drawSmallCircleVertexArray();
		}
	}
	Q_ASSERT(smallCircleVertexArray.isEmpty());
}

void StelPainter::drawPath(const QVector<Vec3d> &points, const QVector<Vec4f> &colors)
{
	// Because the path may intersect a viewport discontinuity, we cannot render
	// it in one OpenGL drawing call.
	Q_ASSERT(smallCircleVertexArray.isEmpty());
	Q_ASSERT(smallCircleColorArray.isEmpty());
	Q_ASSERT(points.size() == colors.size());
	Vec3d win;

	// In general we should add all the points, even if they are hidden, since otherwise we don't
	// have proper clipping on the sides.  We make an exception for the orthographic projection because
	// its clipping doesn't work well.  A better solution would be to use a culling test I think.
	bool skipHiddenPoints = dynamic_cast<StelProjectorOrthographic*>(prj.data());

	for (int i = 0; i+1 != points.size(); i++)
	{
		const Vec3d p1 = points[i];
		const Vec3d p2 = points[i + 1];
		if (!prj->intersectViewportDiscontinuity(p1, p2))
		{
			bool visible = prj->project(p1, win);

			if (!visible && skipHiddenPoints)
			{
				drawSmallCircleVertexArray();
				continue;
			}

			smallCircleVertexArray.append(Vec3f(static_cast<float>(win[0]), static_cast<float>(win[1]), static_cast<float>(win[2])));
			smallCircleColorArray.append(colors[i]);
			if (i+2==points.size())
			{
				prj->project(p2, win);
				smallCircleVertexArray.append(Vec3f(static_cast<float>(win[0]), static_cast<float>(win[1]), static_cast<float>(win[2])));
				smallCircleColorArray.append(colors[i + 1]);
				drawSmallCircleVertexArray();
			}
		}
		else
		{
			// Break the line, draw the stored vertex and flush the list
			if (!smallCircleVertexArray.isEmpty())
			{
				prj->project(p1, win);
				smallCircleVertexArray.append(Vec3f(static_cast<float>(win[0]), static_cast<float>(win[1]), static_cast<float>(win[2])));
				smallCircleColorArray.append(colors[i]);
			}
			drawSmallCircleVertexArray();
		}
	}
	Q_ASSERT(smallCircleVertexArray.isEmpty());
	Q_ASSERT(smallCircleColorArray.isEmpty());
}

// Project the passed triangle on the screen ensuring that it will look smooth, even for non linear distortion
// by splitting it into subtriangles.
void StelPainter::projectSphericalTriangle(const SphericalCap* clippingCap, const Vec3d* vertices, QVarLengthArray<Vec3f, 4096>* outVertices,
        const Vec2f* texturePos, QVarLengthArray<Vec2f, 4096>* outTexturePos, const Vec3f *colors, QVarLengthArray<Vec3f, 4096> *outColors,
        double maxSqDistortion, int nbI, bool checkDisc1, bool checkDisc2, bool checkDisc3) const
{
	Q_ASSERT(fabs(vertices[0].norm()-1.)<0.00001);
	Q_ASSERT(fabs(vertices[1].norm()-1.)<0.00001);
	Q_ASSERT(fabs(vertices[2].norm()-1.)<0.00001);
	if (clippingCap && clippingCap->containsTriangle(vertices))
		clippingCap = Q_NULLPTR;
	if (clippingCap && !clippingCap->intersectsTriangle(vertices))
		return;
	bool cDiscontinuity1 = checkDisc1 && prj->intersectViewportDiscontinuity(vertices[0], vertices[1]);
	bool cDiscontinuity2 = checkDisc2 && prj->intersectViewportDiscontinuity(vertices[1], vertices[2]);
	bool cDiscontinuity3 = checkDisc3 && prj->intersectViewportDiscontinuity(vertices[0], vertices[2]);
	const bool cd1=cDiscontinuity1;
	const bool cd2=cDiscontinuity2;
	const bool cd3=cDiscontinuity3;

	Vec3d e0=vertices[0];
	Vec3d e1=vertices[1];
	Vec3d e2=vertices[2];
	bool valid = prj->projectInPlace(e0);
	valid = prj->projectInPlace(e1) || valid;
	valid = prj->projectInPlace(e2) || valid;
	// Clip polygons behind the viewer
	if (!valid)
		return;

	if (checkDisc1 && cDiscontinuity1==false)
	{
		// If the distortion at segment e0,e1 is too big, flags it for subdivision
		Vec3d win3 = vertices[0]; win3+=vertices[1];
		prj->projectInPlace(win3);
		win3[0]-=(e0[0]+e1[0])*0.5; win3[1]-=(e0[1]+e1[1])*0.5;
		cDiscontinuity1 = (win3[0]*win3[0]+win3[1]*win3[1])>maxSqDistortion;
	}
	if (checkDisc2 && cDiscontinuity2==false)
	{
		// If the distortion at segment e1,e2 is too big, flags it for subdivision
		Vec3d win3 = vertices[1]; win3+=vertices[2];
		prj->projectInPlace(win3);
		win3[0]-=(e2[0]+e1[0])*0.5; win3[1]-=(e2[1]+e1[1])*0.5;
		cDiscontinuity2 = (win3[0]*win3[0]+win3[1]*win3[1])>maxSqDistortion;
	}
	if (checkDisc3 && cDiscontinuity3==false)
	{
		// If the distortion at segment e2,e0 is too big, flags it for subdivision
		Vec3d win3 = vertices[2]; win3+=vertices[0];
		prj->projectInPlace(win3);
		win3[0] -= (e0[0]+e2[0])*0.5;
		win3[1] -= (e0[1]+e2[1])*0.5;
		cDiscontinuity3 = (win3[0]*win3[0]+win3[1]*win3[1])>maxSqDistortion;
	}

	if (!cDiscontinuity1 && !cDiscontinuity2 && !cDiscontinuity3)
	{
		// The triangle is clean, appends it
		outVertices->append(e0.toVec3f()); outVertices->append(e1.toVec3f()); outVertices->append(e2.toVec3f());
		if (outTexturePos)
			outTexturePos->append(texturePos,3);
		if (outColors)
			outColors->append(colors,3);
		return;
	}

	if (nbI > 4)
	{
		// If we reached the limit number of iterations and still have a discontinuity,
		// discards the triangle.
		if (cd1 || cd2 || cd3)
			return;

		// Else display it, it will be suboptimal though.
		outVertices->append(e0.toVec3f()); outVertices->append(e1.toVec3f()); outVertices->append(e2.toVec3f());
		if (outTexturePos)
			outTexturePos->append(texturePos,3);
		if (outColors)
			outColors->append(colors,3);
		return;
	}

	// Recursively splits the triangle into sub triangles.
	// Depending on which combination of sides of the triangle has to be split a different strategy is used.
	Vec3d va[3];
	Vec2f ta[3];
	Vec3f ca[3];
	// Only 1 side has to be split: split the triangle in 2
	if (cDiscontinuity1 && !cDiscontinuity2 && !cDiscontinuity3)
	{
		va[0]=vertices[0];
		va[1]=vertices[0];va[1]+=vertices[1];
		va[1].normalize();
		va[2]=vertices[2];
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=(texturePos[0]+texturePos[1])*0.5;
			ta[2]=texturePos[2];
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=(colors[0]+colors[1])*0.5;
			ca[2]=colors[2];
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, true, true, false);

		//va[0]=vertices[0]+vertices[1];
		//va[0].normalize();
		va[0]=va[1];
		va[1]=vertices[1];
		va[2]=vertices[2];
		if (outTexturePos)
		{
			ta[0]=(texturePos[0]+texturePos[1])*0.5;
			ta[1]=texturePos[1];
			ta[2]=texturePos[2];
		}
		if (outColors)
		{
			ca[0]=(colors[0]+colors[1])*0.5;
			ca[1]=colors[1];
			ca[2]=colors[2];
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, true, false, true);
		return;
	}

	if (!cDiscontinuity1 && cDiscontinuity2 && !cDiscontinuity3)
	{
		va[0]=vertices[0];
		va[1]=vertices[1];
		va[2]=vertices[1];va[2]+=vertices[2];
		va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=texturePos[1];
			ta[2]=(texturePos[1]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=colors[1];
			ca[2]=(colors[1]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, false, true, true);

		va[0]=vertices[0];
		//va[1]=vertices[1]+vertices[2];
		//va[1].normalize();
		va[1]=va[2];
		va[2]=vertices[2];
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=(texturePos[1]+texturePos[2])*0.5;
			ta[2]=texturePos[2];
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=(colors[1]+colors[2])*0.5;
			ca[2]=colors[2];
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, true, true, false);
		return;
	}

	if (!cDiscontinuity1 && !cDiscontinuity2 && cDiscontinuity3)
	{
		va[0]=vertices[0];
		va[1]=vertices[1];
		va[2]=vertices[0];va[2]+=vertices[2];
		va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=texturePos[1];
			ta[2]=(texturePos[0]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=colors[1];
			ca[2]=(colors[0]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, false, true, true);

		//va[0]=vertices[0]+vertices[2];
		//va[0].normalize();
		va[0]=va[2];
		va[1]=vertices[1];
		va[2]=vertices[2];
		if (outTexturePos)
		{
			ta[0]=(texturePos[0]+texturePos[2])*0.5;
			ta[1]=texturePos[1];
			ta[2]=texturePos[2];
		}
		if (outColors)
		{
			ca[0]=(colors[0]+colors[2])*0.5;
			ca[1]=colors[1];
			ca[2]=colors[2];
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, true, false, true);
		return;
	}

	// 2 sides have to be split: split the triangle in 3
	if (cDiscontinuity1 && cDiscontinuity2 && !cDiscontinuity3)
	{
		va[0]=vertices[0];
		va[1]=vertices[0];va[1]+=vertices[1];
		va[1].normalize();
		va[2]=vertices[1];va[2]+=vertices[2];
		va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=(texturePos[0]+texturePos[1])*0.5;
			ta[2]=(texturePos[1]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=(colors[0]+colors[1])*0.5;
			ca[2]=(colors[1]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

		//va[0]=vertices[0]+vertices[1];
		//va[0].normalize();
		va[0]=va[1];
		va[1]=vertices[1];
		//va[2]=vertices[1]+vertices[2];
		//va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=(texturePos[0]+texturePos[1])*0.5;
			ta[1]=texturePos[1];
			ta[2]=(texturePos[1]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=(colors[0]+colors[1])*0.5;
			ca[1]=colors[1];
			ca[2]=(colors[1]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

		va[0]=vertices[0];
		//va[1]=vertices[1]+vertices[2];
		//va[1].normalize();
		va[1]=va[2];
		va[2]=vertices[2];
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=(texturePos[1]+texturePos[2])*0.5;
			ta[2]=texturePos[2];
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=(colors[1]+colors[2])*0.5;
			ca[2]=colors[2];
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, true, true, false);
		return;
	}
	if (cDiscontinuity1 && !cDiscontinuity2 && cDiscontinuity3)
	{
		va[0]=vertices[0];
		va[1]=vertices[0];va[1]+=vertices[1];
		va[1].normalize();
		va[2]=vertices[0];va[2]+=vertices[2];
		va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=(texturePos[0]+texturePos[1])*0.5;
			ta[2]=(texturePos[0]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=(colors[0]+colors[1])*0.5;
			ca[2]=(colors[0]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

		//va[0]=vertices[0]+vertices[1];
		//va[0].normalize();
		va[0]=va[1];
		va[1]=vertices[2];
		//va[2]=vertices[0]+vertices[2];
		//va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=(texturePos[0]+texturePos[1])*0.5;
			ta[1]=texturePos[2];
			ta[2]=(texturePos[0]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=(colors[0]+colors[1])*0.5;
			ca[1]=colors[2];
			ca[2]=(colors[0]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);


		//va[0]=vertices[0]+vertices[1];
		//va[0].normalize();
		va[1]=vertices[1];
		va[2]=vertices[2];
		if (outTexturePos)
		{
			ta[0]=(texturePos[0]+texturePos[1])*0.5;
			ta[1]=texturePos[1];
			ta[2]=texturePos[2];
		}
		if (outColors)
		{
			ca[0]=(colors[0]+colors[1])*0.5;
			ca[1]=colors[1];
			ca[2]=colors[2];
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, true, false, true);

		return;
	}
	if (!cDiscontinuity1 && cDiscontinuity2 && cDiscontinuity3)
	{
		va[0]=vertices[0];
		va[1]=vertices[1];
		va[2]=vertices[1];va[2]+=vertices[2];
		va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=texturePos[1];
			ta[2]=(texturePos[1]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=colors[1];
			ca[2]=(colors[1]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1, false, true, true);

		//va[0]=vertices[1]+vertices[2];
		//va[0].normalize();
		va[0]=va[2];
		va[1]=vertices[2];
		va[2]=vertices[0];va[2]+=vertices[2];
		va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=(texturePos[1]+texturePos[2])*0.5;
			ta[1]=texturePos[2];
			ta[2]=(texturePos[0]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=(colors[1]+colors[2])*0.5;
			ca[1]=colors[2];
			ca[2]=(colors[0]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

		va[1]=va[0];
		va[0]=vertices[0];
		//va[1]=vertices[1]+vertices[2];
		//va[1].normalize();
		//va[2]=vertices[0]+vertices[2];
		//va[2].normalize();
		if (outTexturePos)
		{
			ta[0]=texturePos[0];
			ta[1]=(texturePos[1]+texturePos[2])*0.5;
			ta[2]=(texturePos[0]+texturePos[2])*0.5;
		}
		if (outColors)
		{
			ca[0]=colors[0];
			ca[1]=(colors[1]+colors[2])*0.5;
			ca[2]=(colors[0]+colors[2])*0.5;
		}
		projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);
		return;
	}

	// Last case: the 3 sides have to be split: cut in 4 triangles a' la HTM
	va[0]=vertices[0];va[0]+=vertices[1];
	va[0].normalize();
	va[1]=vertices[1];va[1]+=vertices[2];
	va[1].normalize();
	va[2]=vertices[0];va[2]+=vertices[2];
	va[2].normalize();
	if (outTexturePos)
	{
		ta[0]=(texturePos[0]+texturePos[1])*0.5;
		ta[1]=(texturePos[1]+texturePos[2])*0.5;
		ta[2]=(texturePos[0]+texturePos[2])*0.5;
	}
	if (outColors)
	{
		ca[0]=(colors[0]+colors[1])*0.5;
		ca[1]=(colors[1]+colors[2])*0.5;
		ca[2]=(colors[0]+colors[2])*0.5;
	}
	projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

	va[1]=va[0];
	va[0]=vertices[0];
	//va[1]=vertices[0]+vertices[1];
	//va[1].normalize();
	//va[2]=vertices[0]+vertices[2];
	//va[2].normalize();
	if (outTexturePos)
	{
		ta[0]=texturePos[0];
		ta[1]=(texturePos[0]+texturePos[1])*0.5;
		ta[2]=(texturePos[0]+texturePos[2])*0.5;
	}
	if (outColors)
	{
		ca[0]=colors[0];
		ca[1]=(colors[0]+colors[1])*0.5;
		ca[2]=(colors[0]+colors[2])*0.5;
	}
	projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

	//va[0]=vertices[0]+vertices[1];
	//va[0].normalize();
	va[0]=va[1];
	va[1]=vertices[1];
	va[2]=vertices[1];va[2]+=vertices[2];
	va[2].normalize();
	if (outTexturePos)
	{
		ta[0]=(texturePos[0]+texturePos[1])*0.5;
		ta[1]=texturePos[1];
		ta[2]=(texturePos[1]+texturePos[2])*0.5;
	}
	if (outColors)
	{
		ca[0]=(colors[0]+colors[1])*0.5;
		ca[1]=colors[1];
		ca[2]=(colors[1]+colors[2])*0.5;
	}
	projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

	va[0]=vertices[0];va[0]+=vertices[2];
	va[0].normalize();
	//va[1]=vertices[1]+vertices[2];
	//va[1].normalize();
	va[1]=va[2];
	va[2]=vertices[2];
	if (outTexturePos)
	{
		ta[0]=(texturePos[0]+texturePos[2])*0.5;
		ta[1]=(texturePos[1]+texturePos[2])*0.5;
		ta[2]=texturePos[2];
	}
	if (outColors)
	{
		ca[0]=(colors[0]+colors[2])*0.5;
		ca[1]=(colors[1]+colors[2])*0.5;
		ca[2]=colors[2];
	}
	projectSphericalTriangle(clippingCap, va, outVertices, ta, outTexturePos, ca, outColors, maxSqDistortion, nbI+1);

	return;
}

QVarLengthArray<Vec3f, 4096> StelPainter::polygonVertexArray;
QVarLengthArray<Vec2f, 4096> StelPainter::polygonTextureCoordArray;
QVarLengthArray<Vec3f, 4096> StelPainter::polygonColorArray;
QVarLengthArray<unsigned int, 4096> StelPainter::indexArray;

void StelPainter::drawGreatCircleArcs(const StelVertexArray& va, const SphericalCap* clippingCap)
{
	Q_ASSERT(va.vertex.size()!=1);
	Q_ASSERT(!va.isIndexed());	// Indexed unsupported yet
	switch (va.primitiveType)
	{
		case StelVertexArray::Lines:
			Q_ASSERT(va.vertex.size()%2==0);
			for (int i=0;i<va.vertex.size();i+=2)
				drawGreatCircleArc(va.vertex.at(i), va.vertex.at(i+1), clippingCap);
			return;
		case StelVertexArray::LineStrip:
			for (int i=0;i<va.vertex.size()-1;++i)
				drawGreatCircleArc(va.vertex.at(i), va.vertex.at(i+1), clippingCap);
			return;
		case StelVertexArray::LineLoop:
			for (int i=0;i<va.vertex.size()-1;++i)
				drawGreatCircleArc(va.vertex.at(i), va.vertex.at(i+1), clippingCap);
			drawGreatCircleArc(va.vertex.last(), va.vertex.first(), clippingCap);
			return;
		default:
			Q_ASSERT(0); // Unsupported primitive yype
	}
}

// The function object that we use as an interface between VertexArray::foreachTriangle and
// StelPainter::projectSphericalTriangle.
//
// This is used by drawSphericalTriangles to project all the triangles coordinates in a StelVertexArray into our global
// vertex array buffer.
class VertexArrayProjector
{
public:
	VertexArrayProjector(const StelVertexArray& ar, StelPainter* apainter, const SphericalCap* aclippingCap,
						 QVarLengthArray<Vec3f, 4096>* aoutVertices, QVarLengthArray<Vec2f, 4096>* aoutTexturePos=Q_NULLPTR, QVarLengthArray<Vec3f, 4096>* aoutColors=Q_NULLPTR, double amaxSqDistortion=5.)
		   : //vertexArray(ar),
		     painter(apainter), clippingCap(aclippingCap), outVertices(aoutVertices),
			 outColors(aoutColors), outTexturePos(aoutTexturePos), maxSqDistortion(amaxSqDistortion)
	{
		Q_UNUSED(ar)
	}

	// Project a single triangle and add it into the output arrays
	inline void operator()(const Vec3d* v0, const Vec3d* v1, const Vec3d* v2,
						   const Vec2f* t0, const Vec2f* t1, const Vec2f* t2,
						   const Vec3f* c0, const Vec3f* c1, const Vec3f* c2,
						   unsigned int, unsigned int, unsigned) const
	{
		// XXX: we may optimize more by putting the declaration and the test outside of this method.
		Vec3d tmpVertex[3] = {*v0, *v1, *v2};
		// required, else assertion at begin of projectSphericalTriangle() fails!
		tmpVertex[0].normalize();
		tmpVertex[1].normalize();
		tmpVertex[2].normalize();
		if ( (outTexturePos) && (outColors))
		{
			const Vec2f tmpTexture[3] = {*t0, *t1, *t2};
			const Vec3f tmpColor[3] = {*c0, *c1, *c2};
			painter->projectSphericalTriangle(clippingCap, tmpVertex, outVertices, tmpTexture, outTexturePos, tmpColor, outColors, maxSqDistortion);
		}
		else if (outTexturePos)
		{
			const Vec2f tmpTexture[3] = {*t0, *t1, *t2};
			painter->projectSphericalTriangle(clippingCap, tmpVertex, outVertices, tmpTexture, outTexturePos, Q_NULLPTR, Q_NULLPTR, maxSqDistortion);
		}
		else if (outColors)
		{
			const Vec3f tmpColor[3] = {*c0, *c1, *c2};
			painter->projectSphericalTriangle(clippingCap, tmpVertex, outVertices, Q_NULLPTR, Q_NULLPTR, tmpColor, outColors, maxSqDistortion);
		}
		else
			painter->projectSphericalTriangle(clippingCap, tmpVertex, outVertices, Q_NULLPTR, Q_NULLPTR, Q_NULLPTR, Q_NULLPTR, maxSqDistortion);
	}

	// Draw the resulting arrays
	void drawResult()
	{
		painter->setVertexPointer(3, GL_FLOAT, outVertices->constData());
		if (outTexturePos)
			painter->setTexCoordPointer(2, GL_FLOAT, outTexturePos->constData());
		if (outColors)
			painter->setColorPointer(3, GL_FLOAT, outColors->constData());

		painter->enableClientStates(true, outTexturePos != Q_NULLPTR, outColors != Q_NULLPTR);
		painter->drawFromArray(StelPainter::Triangles, outVertices->size(), 0, false);
		painter->enableClientStates(false);
	}

private:
	StelPainter* painter;
	const SphericalCap* clippingCap;
	QVarLengthArray<Vec3f, 4096>* outVertices;
	QVarLengthArray<Vec3f, 4096>* outColors;
	QVarLengthArray<Vec2f, 4096>* outTexturePos;
	double maxSqDistortion;
};

void StelPainter::drawStelVertexArray(const StelVertexArray& arr, bool checkDiscontinuity, Vec3d aberration)
{
	if (checkDiscontinuity && prj->hasDiscontinuity())
	{
		// The projection has discontinuities, so we need to make sure that no triangle is crossing them.
		drawStelVertexArray(arr.removeDiscontinuousTriangles(this->getProjector().data()), false, aberration);
		return;
	}

	if (aberration==Vec3d(0.))
	{
		setVertexPointer(3, GL_DOUBLE, arr.vertex.constData());
	}
	else
	{
		QVector<Vec3d> aberredVertex(arr.vertex.size());
		for (int i=0; i<arr.vertex.size(); i++)
		{
			Q_ASSERT(qFuzzyCompare(arr.vertex.at(i).normSquared(), 1.0));
			Vec3d vec=arr.vertex.at(i)+aberration;
			vec.normalize();
			aberredVertex[i]=vec;
		}
		setVertexPointer(3, GL_DOUBLE, aberredVertex.constData());
	}
	if (arr.isTextured())
	{
		setTexCoordPointer(2, GL_FLOAT, arr.texCoords.constData());
		if (arr.isColored())
		{
			setColorPointer(3, GL_FLOAT, arr.colors.constData());
			enableClientStates(true, true, true);
		}
		else
			enableClientStates(true, true, false);
	}
	else
	{
		if (arr.isColored())
		{
			setColorPointer(3, GL_FLOAT, arr.colors.constData());
			enableClientStates(true, false, true);
		}
		else
			enableClientStates(true, false, false);
	}
	if (arr.isIndexed())
		drawFromArray(static_cast<StelPainter::DrawingMode>(arr.primitiveType), arr.indices.size(), 0, true, arr.indices.constData());
	else
		drawFromArray(static_cast<StelPainter::DrawingMode>(arr.primitiveType), arr.vertex.size());

	enableClientStates(false);
}

void StelPainter::drawSphericalTriangles(const StelVertexArray& va, bool textured, bool colored, const SphericalCap* clippingCap, bool doSubDivide, double maxSqDistortion)
{
	if (va.vertex.isEmpty())
		return;

	Q_ASSERT(va.vertex.size()>2);
	polygonVertexArray.clear();
	polygonTextureCoordArray.clear();

	indexArray.clear();

	if (!doSubDivide)
	{
		// The simplest case, we don't need to iterate through the triangles at all.
		drawStelVertexArray(va);
		return;
	}

	// the last case.  It is the slowest, it process the triangles one by one.
	{
		// Project all the triangles of the VertexArray into our buffer arrays.
		VertexArrayProjector result = va.foreachTriangle(VertexArrayProjector(va, this, clippingCap, &polygonVertexArray, textured ? &polygonTextureCoordArray : Q_NULLPTR, colored ? &polygonColorArray : Q_NULLPTR, maxSqDistortion));
		result.drawResult();
		return;
	}
}

// Draw the given SphericalPolygon.
void StelPainter::drawSphericalRegion(SphericalRegion* poly, SphericalPolygonDrawMode drawMode, const SphericalCap* clippingCap, const bool doSubDivise, const double maxSqDistortion, const Vec3d &observerVelocity)
{
	if (!prj->getBoundingCap().intersects(poly->getBoundingCap()))
		return;

	bool oldCullFace = glState.cullFace;

	switch (drawMode)
	{
		case SphericalPolygonDrawModeBoundary:
			if (doSubDivise || prj->intersectViewportDiscontinuity(poly->getBoundingCap()))
				drawGreatCircleArcs(poly->getOutlineVertexArray(), clippingCap);
			else
				drawStelVertexArray(poly->getOutlineVertexArray(), false);
			break;
		case SphericalPolygonDrawModeFill:
		case SphericalPolygonDrawModeTextureFill:
		case SphericalPolygonDrawModeTextureFillColormodulated:
			setCullFace(true);
			// The polygon is already tessellated as triangles
			if (doSubDivise || prj->intersectViewportDiscontinuity(poly->getBoundingCap()))
				// flag for color-modulated textured mode (e.g. for Milky Way/extincted)
				drawSphericalTriangles(poly->getFillVertexArray(observerVelocity), drawMode>=SphericalPolygonDrawModeTextureFill, drawMode==SphericalPolygonDrawModeTextureFillColormodulated, clippingCap, doSubDivise, maxSqDistortion);
			else
				drawStelVertexArray(poly->getFillVertexArray(observerVelocity), false);

			setCullFace(oldCullFace);
			break;
		default:
			Q_ASSERT(0);
	}
}


/*************************************************************************
 draw a simple circle, 2d viewport coordinates in pixel
*************************************************************************/
void StelPainter::drawCircle(float x, float y, float r)
{
	if (r <= 1.0f)
		return;
	const Vec2f center(x,y);
	const Vec2f v_center(0.5f*prj->viewportXywh[2],0.5f*prj->viewportXywh[3]);
	const float R = v_center.norm();
	const float d = (v_center-center).norm();
	if (d > r+R || d < r-R)
		return;
	const int segments = 180;
	const float phi = 2.0f*M_PIf/segments;
	const float cp = std::cos(phi);
	const float sp = std::sin(phi);
	float dx = r;
	float dy = 0;
	static QVarLengthArray<Vec3f, 180> circleVertexArray(180);

	for (int i=0;i<segments;i++)
	{
		circleVertexArray[i].set(x+dx,y+dy,0);
		r = dx*cp-dy*sp;
		dy = dx*sp+dy*cp;
		dx = r;
	}
	enableClientStates(true);
	setVertexPointer(3, GL_FLOAT, circleVertexArray.data());
	drawFromArray(LineLoop, 180, 0, false);
	enableClientStates(false);
}

// rx: radius in x axis
// ry: radius in y axis
// angle rotation (counterclockwise), radians [0..2pi]
void StelPainter::drawEllipse(double x, double y, double rX, double rY, double angle)
{
	if (rX <= 1.0 || rY <= 1.0)
		return;

	//const float radiusY = 0.35 * size;
	//const float radiusX = aspectRatio * radiusY;
	const int numPoints = std::lround(std::clamp(qMax(rX, rY)/3, 32., 1024.));
	std::vector<float> vertexData;
	vertexData.reserve(numPoints*2);
	const float*const cossin = StelUtils::ComputeCosSinTheta(numPoints);
	const auto cosa = std::cos(angle);
	const auto sina = std::sin(angle);
	for(int n = 0; n < numPoints; ++n)
	{
		const auto cosb = cossin[2*n], sinb = cossin[2*n+1];
		const auto pointX = rX*sinb;
		const auto pointY = rY*cosb;
		vertexData.push_back(x + pointX*cosa - pointY*sina);
		vertexData.push_back(y + pointY*cosa + pointX*sina);
	}
	const auto vertCount = vertexData.size() / 2;
	setLineSmooth(true);
	setLineWidth(std::clamp(qMax(rX, rY)/40, 1., 2.));
	enableClientStates(true);
	setVertexPointer(2, GL_FLOAT, vertexData.data());
	drawFromArray(StelPainter::LineLoop, vertCount, 0, false);
	enableClientStates(false);
}

void StelPainter::drawSprite2dMode(float x, float y, float radius)
{
	static float vertexData[] = {-10.,-10.,10.,-10., 10.,10., -10.,10.};
	static const float texCoordData[] = {0.,0., 1.,0., 0.,1., 1.,1.};
	
	// Takes into account device pixel density and global scale ratio, as we are drawing 2D stuff.
	radius *= static_cast<float>(prj->getDevicePixelsPerPixel());
	
	vertexData[0]=x-radius; vertexData[1]=y-radius;
	vertexData[2]=x+radius; vertexData[3]=y-radius;
	vertexData[4]=x-radius; vertexData[5]=y+radius;
	vertexData[6]=x+radius; vertexData[7]=y+radius;
	enableClientStates(true, true);
	setVertexPointer(2, GL_FLOAT, vertexData);
	setTexCoordPointer(2, GL_FLOAT, texCoordData);
	drawFromArray(TriangleStrip, 4, 0, false);
	enableClientStates(false);
}

void StelPainter::drawSprite2dModeNoDeviceScale(float x, float y, float radius)
{
	drawSprite2dMode(x, y, radius/(static_cast<float>(prj->getDevicePixelsPerPixel())));
}

void StelPainter::drawSprite2dMode(const Vec3d& v, float radius)
{
	Vec3d win;
	if (prj->project(v, win))
		drawSprite2dMode(static_cast<float>(win[0]), static_cast<float>(win[1]), radius);
}

void StelPainter::drawSprite2dMode(float x, float y, float radius, float rotation)
{
	static float vertexData[8];
	static const float texCoordData[] = {0.f,0.f, 1.f,0.f, 0.f,1.f, 1.f,1.f};

	// compute the vertex coordinates applying the translation and the rotation
	static const float vertexBase[] = {-1., -1., 1., -1., -1., 1., 1., 1.};
	const float cosr = std::cos(rotation * M_PI_180f);
	const float sinr = std::sin(rotation * M_PI_180f);
	
	// Takes into account device pixel density and global scale ratio, as we are drawing 2D stuff.
	radius *= static_cast<float>(prj->getDevicePixelsPerPixel());
	
	for (int i = 0; i < 8; i+=2)
	{
		vertexData[i] = x + radius * vertexBase[i] * cosr - radius * vertexBase[i+1] * sinr;
		vertexData[i+1] = y + radius * vertexBase[i] * sinr + radius * vertexBase[i+1] * cosr;
	}

	enableClientStates(true, true);
	setVertexPointer(2, GL_FLOAT, vertexData);
	setTexCoordPointer(2, GL_FLOAT, texCoordData);
	drawFromArray(TriangleStrip, 4, 0, false);
	enableClientStates(false);
}

void StelPainter::drawRect2d(float x, float y, float width, float height, bool textured)
{
	static float vertexData[] = {-10.,-10.,10.,-10., 10.,10., -10.,10.};
	static const float texCoordData[] = {0.,0., 1.,0., 0.,1., 1.,1.};
	vertexData[0]=x; vertexData[1]=y;
	vertexData[2]=x+width; vertexData[3]=y;
	vertexData[4]=x; vertexData[5]=y+height;
	vertexData[6]=x+width; vertexData[7]=y+height;
	if (textured)
	{
		enableClientStates(true, true);
		setVertexPointer(2, GL_FLOAT, vertexData);
		setTexCoordPointer(2, GL_FLOAT, texCoordData);
	}
	else
	{
		enableClientStates(true);
		setVertexPointer(2, GL_FLOAT, vertexData);
	}
	drawFromArray(TriangleStrip, 4, 0, false);
	enableClientStates(false);
}

/*************************************************************************
 Draw a GL_POINT at the given position
*************************************************************************/
void StelPainter::drawPoint2d(float x, float y)
{
	static float vertexData[] = {0.,0.};
	vertexData[0]=x;
	vertexData[1]=y;

	enableClientStates(true);
	setVertexPointer(2, GL_FLOAT, vertexData);
	drawFromArray(Points, 1, 0, false);
	enableClientStates(false);
}


/*************************************************************************
 Draw a line between the 2 points.
*************************************************************************/
void StelPainter::drawLine2d(const float x1, const float y1, const float x2, const float y2)
{
	static float vertexData[] = {0.,0.,0.,0.};
	vertexData[0]=x1;
	vertexData[1]=y1;
	vertexData[2]=x2;
	vertexData[3]=y2;

	enableClientStates(true);
	setVertexPointer(2, GL_FLOAT, vertexData);
	drawFromArray(Lines, 2, 0, false);
	enableClientStates(false);
}

///////////////////////////////////////////////////////////////////////////
// Drawing methods for general (non-linear) mode.
// This used to draw a full sphere. Since 0.13 it's possible to have a spherical zone only.
void StelPainter::sSphere(const double radius, const double oneMinusOblateness,
			  const unsigned int slices, const unsigned int stacks,
			  const bool orientInside, const bool flipTexture, const float topAngle, const float bottomAngle)
{
	double x, y, z;
	GLfloat s=0.f, t=0.f;
	GLfloat nsign;

	if (orientInside)
	{
		nsign = -1.f;
		t=0.f; // from inside texture is reversed
	}
	else
	{
		nsign = 1.f;
		t=1.f;
	}

	const float* cos_sin_rho = Q_NULLPTR;
	Q_ASSERT(topAngle<bottomAngle); // don't forget: These are opening angles counted from top.
	if ((bottomAngle>3.1415f) && (topAngle<0.0001f)) // safety margin.
		cos_sin_rho = StelUtils::ComputeCosSinRho(stacks);
	else
	{
		const float drho = (bottomAngle-topAngle) / static_cast<float>(stacks); // deltaRho:  originally just 180degrees/stacks, now the range clamped.
		cos_sin_rho = StelUtils::ComputeCosSinRhoZone(drho, stacks, M_PIf-bottomAngle);
	}
	// Allow parameters so that pole regions may remain free.
	const float* cos_sin_rho_p;

	const float* cos_sin_theta = StelUtils::ComputeCosSinTheta(slices);
	const float *cos_sin_theta_p;

	// texturing: s goes from 0.0/0.25/0.5/0.75/1.0 at +y/+x/-y/-x/+y axis
	// t goes from -1.0/+1.0 at z = -radius/+radius (linear along longitudes)
	// cannot use triangle fan on texturing (s coord. at top/bottom tip varies)
	// If the texture is flipped, we iterate the coordinates backward.
	const GLfloat ds = (flipTexture ? -1.f : 1.f) / static_cast<float>(slices);
	const GLfloat dt = nsign / static_cast<float>(stacks); // from inside texture is reversed

	// draw intermediate as quad strips
	static QVector<double> vertexArr;
	static QVector<float> texCoordArr;
	static QVector<float> colorArr;
	static QVector<unsigned short> indiceArr;

	texCoordArr.resize(0);
	vertexArr.resize(0);
	colorArr.resize(0);
	indiceArr.resize(0);

	// LGTM comments: the floats are always <=1. We still prefer float multiplication (with insignificant accuracy loss) for speed.
	uint i, j;
	for (i = 0,cos_sin_rho_p = cos_sin_rho; i < stacks; ++i,cos_sin_rho_p+=2)
	{
		s = flipTexture ? 1.f : 0.f;
		for (j = 0,cos_sin_theta_p = cos_sin_theta; j<=slices;++j,cos_sin_theta_p+=2)
		{
			x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[1]);
			y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[1]);
			z = static_cast<double>(nsign * cos_sin_rho_p[0]);
			texCoordArr << s << t;
			vertexArr << x * radius << y * radius << z * oneMinusOblateness * radius;
			x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[3]);
			y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[3]);
			z = static_cast<double>(nsign * cos_sin_rho_p[2]);
			texCoordArr << s << t - dt;
			vertexArr << x * radius << y * radius << z * oneMinusOblateness * radius;
			s += ds;
		}
		unsigned int offset = i*(slices+1)*2;
		for (j = 2;j<slices*2+2;j+=2)
		{
			indiceArr << static_cast<unsigned short>(offset+j-2) << static_cast<unsigned short>(offset+j-1) << static_cast<unsigned short>(offset+j);
			indiceArr << static_cast<unsigned short>(offset+j) << static_cast<unsigned short>(offset+j-1) << static_cast<unsigned short>(offset+j+1);
		}
		t -= dt;
	}

	// Draw the array now
	setArrays(reinterpret_cast<const Vec3d*>(vertexArr.constData()), reinterpret_cast<const Vec2f*>(texCoordArr.constData()));
	drawFromArray(Triangles, indiceArr.size(), 0, true, indiceArr.constData());
}

StelVertexArray StelPainter::computeSphereNoLight(double radius, double oneMinusOblateness, unsigned int slices, unsigned int stacks,
                          int orientInside, bool flipTexture, float topAngle, float bottomAngle)
{
	StelVertexArray result(StelVertexArray::Triangles);
	double x, y, z;
	GLfloat s=0.f, t=0.f;
	GLuint i, j;
	GLfloat nsign;
	if (orientInside)
	{
		nsign = -1.f;
		t=0.f; // from inside texture is reversed
	}
	else
	{
		nsign = 1.f;
		t=1.f;
	}

	const float* cos_sin_rho = Q_NULLPTR; //StelUtils::ComputeCosSinRho(stacks);
	Q_ASSERT(topAngle<bottomAngle); // don't forget: These are opening angles counted from top.
	if ((bottomAngle>3.1415f) && (topAngle<0.0001f)) // safety margin.
		cos_sin_rho = StelUtils::ComputeCosSinRho(stacks);
	else
	{
		const float drho = (bottomAngle-topAngle) / static_cast<float>(stacks); // deltaRho:  originally just 180degrees/stacks, now the range clamped.
		cos_sin_rho = StelUtils::ComputeCosSinRhoZone(drho, stacks, M_PIf-bottomAngle);
	}
	// Allow parameters so that pole regions may remain free.
	const float* cos_sin_rho_p;

	const float* cos_sin_theta = StelUtils::ComputeCosSinTheta(slices);
	const float *cos_sin_theta_p;

	// texturing: s goes from 0.0/0.25/0.5/0.75/1.0 at +y/+x/-y/-x/+y axis
	// t goes from -1.0/+1.0 at z = -radius/+radius (linear along longitudes)
	// cannot use triangle fan on texturing (s coord. at top/bottom tip varies)
	// If the texture is flipped, we iterate the coordinates backward.
	const GLfloat ds = (flipTexture ? -1.f : 1.f) / static_cast<float>(slices);
	const GLfloat dt = nsign / static_cast<float>(stacks); // from inside texture is reversed

	// draw intermediate as quad strips
	// LGTM comments: the floats are always <=1. We still prefer float multiplication (with insignificant accuracy loss) for speed.
	for (i = 0,cos_sin_rho_p = cos_sin_rho; i < stacks; ++i,cos_sin_rho_p+=2)
	{
		s = !flipTexture ? 0.f : 1.f;
		for (j = 0,cos_sin_theta_p = cos_sin_theta; j<=slices;++j,cos_sin_theta_p+=2)
		{
			x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[1]);
			y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[1]);
			z = static_cast<double>(nsign * cos_sin_rho_p[0]);
			result.texCoords << Vec2f(s,t);
			result.vertex << Vec3d(x*radius, y*radius, z*oneMinusOblateness*radius);
			x = static_cast<double>(-cos_sin_theta_p[1] * cos_sin_rho_p[3]);
			y = static_cast<double>(cos_sin_theta_p[0] * cos_sin_rho_p[3]);
			z = static_cast<double>(nsign * cos_sin_rho_p[2]);
			result.texCoords << Vec2f(s, t-dt);
			result.vertex << Vec3d(x*radius, y*radius, z*oneMinusOblateness*radius);
			s += ds;
		}
		unsigned int offset = i*(slices+1)*2;
		for (j = 2;j<slices*2+2;j+=2)
		{
			result.indices << static_cast<unsigned short>(offset+j-2) << static_cast<unsigned short>(offset+j-1) << static_cast<unsigned short>(offset+j);
			result.indices << static_cast<unsigned short>(offset+j) << static_cast<unsigned short>(offset+j-1) << static_cast<unsigned short>(offset+j+1);
		}
		t -= dt;
	}
	return result;
}

// Reimplementation of gluCylinder : glu is overridden for non standard projection
void StelPainter::sCylinder(double radius, double height, int slices, int orientInside)
{
	if (orientInside)
		glCullFace(GL_FRONT);

	static QVarLengthArray<Vec2f, 512> texCoordArray;
	static QVarLengthArray<Vec3d, 512> vertexArray;
	texCoordArray.clear();
	vertexArray.clear();
	float s = 0.f;
	double x, y;
	const float ds = 1.f / static_cast<float>(slices);
	const double da = 2. * M_PI / slices;
	for (int i = 0; i <= slices; ++i)
	{
		x = std::sin(da*i);
		y = std::cos(da*i);
		texCoordArray.append(Vec2f(s, 0.f));
		vertexArray.append(Vec3d(x*radius, y*radius, 0.));
		texCoordArray.append(Vec2f(s, 1.f));
		vertexArray.append(Vec3d(x*radius, y*radius, height));
		s += ds;
	}
	setArrays(vertexArray.constData(), texCoordArray.constData());
	drawFromArray(TriangleStrip, vertexArray.size());

	if (orientInside)
		glCullFace(GL_BACK);
}

void StelPainter::initGLShaders()
{
	qInfo() << "Initializing basic GL shaders... ";
	// Basic shader: just vertex filled with plain color
	QOpenGLShader vshader3(QOpenGLShader::Vertex);
	const auto vsrc3 =
		StelOpenGL::globalShaderPrefix(StelOpenGL::VERTEX_SHADER) +
		"ATTRIBUTE mediump vec3 vertex;\n"
		"uniform mediump mat4 projectionMatrix;\n"
		"void main(void)\n"
		"{\n"
		"    gl_Position = projectionMatrix*vec4(vertex, 1.);\n"
		"}\n";
	vshader3.compileSourceCode(vsrc3);
	if (!vshader3.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling vshader3: " << vshader3.log();
	QOpenGLShader fshader3(QOpenGLShader::Fragment);
	const auto fsrc3 =
		StelOpenGL::globalShaderPrefix(StelOpenGL::FRAGMENT_SHADER) +
		"uniform mediump vec4 color;\n"
		"void main(void)\n"
		"{\n"
		"    FRAG_COLOR = color;\n"
		"}\n";
	fshader3.compileSourceCode(fsrc3);
	if (!fshader3.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling fshader3: " << fshader3.log();
	basicShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	basicShaderProgram->addShader(&vshader3);
	basicShaderProgram->addShader(&fshader3);
	linkProg(basicShaderProgram, "basicShaderProgram");
	basicShaderVars.projectionMatrix = basicShaderProgram->uniformLocation("projectionMatrix");
	basicShaderVars.color = basicShaderProgram->uniformLocation("color");
	basicShaderVars.vertex = basicShaderProgram->attributeLocation("vertex");
	

	// Basic shader: vertex filled with interpolated color
	QOpenGLShader vshaderInterpolatedColor(QOpenGLShader::Vertex);
	const auto vshaderInterpolatedColorSrc =
		StelOpenGL::globalShaderPrefix(StelOpenGL::VERTEX_SHADER) +
		"ATTRIBUTE mediump vec3 vertex;\n"
		"ATTRIBUTE mediump vec4 color;\n"
		"uniform mediump mat4 projectionMatrix;\n"
		"VARYING mediump vec4 fragcolor;\n"
		"void main(void)\n"
		"{\n"
		"    gl_Position = projectionMatrix*vec4(vertex, 1.);\n"
		"    fragcolor = color;\n"
		"}\n";
	vshaderInterpolatedColor.compileSourceCode(vshaderInterpolatedColorSrc);
	if (!vshaderInterpolatedColor.log().isEmpty()) {
	  qWarning().noquote() << "StelPainter: Warnings while compiling vshaderInterpolatedColor: " << vshaderInterpolatedColor.log();
	}
	QOpenGLShader fshaderInterpolatedColor(QOpenGLShader::Fragment);
	const auto fshaderInterpolatedColorSrc =
		StelOpenGL::globalShaderPrefix(StelOpenGL::FRAGMENT_SHADER) +
		"VARYING mediump vec4 fragcolor;\n"
		"void main(void)\n"
		"{\n"
		"    FRAG_COLOR = fragcolor;\n"
		"}\n";
	fshaderInterpolatedColor.compileSourceCode(fshaderInterpolatedColorSrc);
	if (!fshaderInterpolatedColor.log().isEmpty()) {
	  qWarning().noquote() << "StelPainter: Warnings while compiling fshaderInterpolatedColor: " << fshaderInterpolatedColor.log();
	}
	colorShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	colorShaderProgram->addShader(&vshaderInterpolatedColor);
	colorShaderProgram->addShader(&fshaderInterpolatedColor);
	linkProg(colorShaderProgram, "colorShaderProgram");
	colorShaderVars.projectionMatrix = colorShaderProgram->uniformLocation("projectionMatrix");
	colorShaderVars.color = colorShaderProgram->attributeLocation("color");
	colorShaderVars.vertex = colorShaderProgram->attributeLocation("vertex");
	
	// Text shader program
	QOpenGLShader textVShader(QOpenGLShader::Vertex);
	const auto textVSrc =
		StelOpenGL::globalShaderPrefix(StelOpenGL::VERTEX_SHADER) + R"(
ATTRIBUTE highp vec3 vertex;
ATTRIBUTE mediump vec2 texCoord;
uniform mediump mat4 projectionMatrix;
VARYING mediump vec2 texc;
void main()
{
	gl_Position = projectionMatrix * vec4(vertex, 1.);
	texc = texCoord;
})";
	textVShader.compileSourceCode(textVSrc);
	if (!textVShader.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling text vertex shader: " << textVShader.log();

	QOpenGLShader textFShader(QOpenGLShader::Fragment);
	const auto textFSrc =
		StelOpenGL::globalShaderPrefix(StelOpenGL::FRAGMENT_SHADER) + R"(
VARYING mediump vec2 texc;
uniform sampler2D tex;
uniform mediump vec4 textColor;
void main()
{
	float mask = texture2D(tex, texc).r;
	FRAG_COLOR = vec4(textColor.rgb, textColor.a*mask);
})";
	textFShader.compileSourceCode(textFSrc);
	if (!textFShader.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling text fragment shader: " << textFShader.log();

	textShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	textShaderProgram->addShader(&textVShader);
	textShaderProgram->addShader(&textFShader);
	linkProg(textShaderProgram, "textShaderProgram");
	textShaderVars.projectionMatrix = textShaderProgram->uniformLocation("projectionMatrix");
	textShaderVars.texCoord = textShaderProgram->attributeLocation("texCoord");
	textShaderVars.vertex = textShaderProgram->attributeLocation("vertex");
	textShaderVars.textColor = textShaderProgram->uniformLocation("textColor");
	textShaderVars.texture = textShaderProgram->uniformLocation("tex");

	// Basic texture shader program
	QOpenGLShader vshader2(QOpenGLShader::Vertex);
	const auto vsrc2 =
		StelOpenGL::globalShaderPrefix(StelOpenGL::VERTEX_SHADER) +
		"ATTRIBUTE highp vec3 vertex;\n"
		"ATTRIBUTE mediump vec2 texCoord;\n"
		"uniform mediump mat4 projectionMatrix;\n"
		"VARYING mediump vec2 texc;\n"
		"void main(void)\n"
		"{\n"
		"    gl_Position = projectionMatrix * vec4(vertex, 1.);\n"
		"    texc = texCoord;\n"
		"}\n";
	vshader2.compileSourceCode(vsrc2);
	if (!vshader2.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling vshader2: " << vshader2.log();

	QOpenGLShader fshader2(QOpenGLShader::Fragment);
	const auto fsrc2 =
		StelOpenGL::globalShaderPrefix(StelOpenGL::FRAGMENT_SHADER) +
		"VARYING mediump vec2 texc;\n"
		"uniform sampler2D tex;\n"
		"uniform mediump vec4 texColor;\n"
		"void main(void)\n"
		"{\n"
		"    FRAG_COLOR = texture2D(tex, texc)*texColor;\n"
		"}\n";
	fshader2.compileSourceCode(fsrc2);
	if (!fshader2.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling fshader2: " << fshader2.log();

	texturesShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	texturesShaderProgram->addShader(&vshader2);
	texturesShaderProgram->addShader(&fshader2);
	linkProg(texturesShaderProgram, "texturesShaderProgram");
	texturesShaderVars.projectionMatrix = texturesShaderProgram->uniformLocation("projectionMatrix");
	texturesShaderVars.texCoord = texturesShaderProgram->attributeLocation("texCoord");
	texturesShaderVars.vertex = texturesShaderProgram->attributeLocation("vertex");
	texturesShaderVars.texColor = texturesShaderProgram->uniformLocation("texColor");
	texturesShaderVars.texture = texturesShaderProgram->uniformLocation("tex");

	// Texture shader program + interpolated color per vertex
	QOpenGLShader vshader4(QOpenGLShader::Vertex);
	const auto vsrc4 =
		StelOpenGL::globalShaderPrefix(StelOpenGL::VERTEX_SHADER) +
		"ATTRIBUTE highp vec3 vertex;\n"
		"ATTRIBUTE mediump vec2 texCoord;\n"
		"ATTRIBUTE mediump vec4 color;\n"
		"uniform mediump mat4 projectionMatrix;\n"
		"VARYING mediump vec2 texc;\n"
		"VARYING mediump vec4 outColor;\n"
		"void main(void)\n"
		"{\n"
		"    gl_Position = projectionMatrix * vec4(vertex, 1.);\n"
		"    texc = texCoord;\n"
		"    outColor = color;\n"
		"}\n";
	vshader4.compileSourceCode(vsrc4);
	if (!vshader4.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling vshader4: " << vshader4.log();

	QOpenGLShader fshader4(QOpenGLShader::Fragment);
	const auto fsrc4 =
		StelOpenGL::globalShaderPrefix(StelOpenGL::FRAGMENT_SHADER) +
		makeSaturationShader()+
		"VARYING mediump vec2 texc;\n"
		"VARYING mediump vec4 outColor;\n"
		"uniform sampler2D tex;\n"
		"uniform lowp float saturation;\n"
		"void main(void)\n"
		"{\n"
		"    FRAG_COLOR = texture2D(tex, texc)*outColor;\n"
		"    if (saturation != 1.0)\n"
		"        FRAG_COLOR.rgb = saturate(FRAG_COLOR.rgb, saturation);\n"
		"}\n";
	fshader4.compileSourceCode(fsrc4);
	if (!fshader4.log().isEmpty())
		qWarning().noquote() << "StelPainter: Warnings while compiling fshader4: " << fshader4.log();

	texturesColorShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
	texturesColorShaderProgram->addShader(&vshader4);
	texturesColorShaderProgram->addShader(&fshader4);
	linkProg(texturesColorShaderProgram, "texturesColorShaderProgram");
	texturesColorShaderVars.projectionMatrix = texturesColorShaderProgram->uniformLocation("projectionMatrix");
	texturesColorShaderVars.texCoord = texturesColorShaderProgram->attributeLocation("texCoord");
	texturesColorShaderVars.vertex = texturesColorShaderProgram->attributeLocation("vertex");
	texturesColorShaderVars.color = texturesColorShaderProgram->attributeLocation("color");
	texturesColorShaderVars.texture = texturesColorShaderProgram->uniformLocation("tex");
	texturesColorShaderVars.saturation = texturesColorShaderProgram->uniformLocation("saturation");

	if(StelMainView::getInstance().getGLInformation().isCoreProfile)
	{
		// In Core profile wide lines (width>1px) are not supported, so this shader is used to render them with triangles.

		// Common template for the geometry shader
		const QByteArray geomSrc = 1+R"(
#version 330

layout(lines) in;
layout(triangle_strip, max_vertices=4) out;
uniform vec2 viewportSize;
uniform float lineWidth;
#if @VARYING_COLOR@
in vec4 varyingColor[2];
out vec4 geomColor;
#endif

void main()
{
	vec4 clip0 = gl_in[0].gl_Position;
	vec4 clip1 = gl_in[1].gl_Position;

	vec3 ndc0 = clip0.xyz / clip0.w;
	vec3 ndc1 = clip1.xyz / clip1.w;

	vec2 lineDir2d = normalize((ndc1.xy - ndc0.xy) * viewportSize);
	vec2 perpendicularDir2d = vec2(-lineDir2d.y, lineDir2d.x);

	vec2 offset2d = (lineWidth / viewportSize) * perpendicularDir2d;

	gl_Position = vec4(clip0.xy + offset2d*clip0.w, clip0.zw);
#if @VARYING_COLOR@
	geomColor = varyingColor[0];
#endif
	EmitVertex();

	gl_Position = vec4(clip0.xy - offset2d*clip0.w, clip0.zw);
#if @VARYING_COLOR@
	geomColor = varyingColor[0];
#endif
	EmitVertex();

	gl_Position = vec4(clip1.xy + offset2d*clip1.w, clip1.zw);
#if @VARYING_COLOR@
	geomColor = varyingColor[1];
#endif
	EmitVertex();

	gl_Position = vec4(clip1.xy - offset2d*clip1.w, clip1.zw);
#if @VARYING_COLOR@
	geomColor = varyingColor[1];
#endif
	EmitVertex();

	EndPrimitive();
}
)";

		// First a basic shader using constant line color
		QOpenGLShader wideLineVertShader(QOpenGLShader::Vertex);
		wideLineVertShader.compileSourceCode(1+R"(
#version 330
in vec3 vertex;
uniform mat4 projectionMatrix;
void main()
{
    gl_Position = projectionMatrix*vec4(vertex, 1.);
}
)");
		if (!wideLineVertShader.log().isEmpty())
			qWarning().noquote() << "StelPainter: Warnings while compiling wide line vertex shader: " << wideLineVertShader.log();

		QOpenGLShader wideLineGeomShader(QOpenGLShader::Geometry);
		wideLineGeomShader.compileSourceCode(QByteArray(geomSrc).replace("@VARYING_COLOR@","0"));
		if (!wideLineGeomShader.log().isEmpty())
			qWarning().noquote() << "StelPainter: Warnings while compiling wide line geometry shader: " << wideLineGeomShader.log();

		QOpenGLShader wideLineFragShader(QOpenGLShader::Fragment);
		wideLineFragShader.compileSourceCode(1+R"(
#version 330
uniform vec4 color;
out vec4 outputColor;
void main()
{
    outputColor = color;
}
)");
		if (!wideLineFragShader.log().isEmpty())
			qWarning().noquote() << "StelPainter: Warnings while compiling wide line fragment shader: " << wideLineFragShader.log();
		wideLineShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
		wideLineShaderProgram->addShader(&wideLineVertShader);
		wideLineShaderProgram->addShader(&wideLineGeomShader);
		wideLineShaderProgram->addShader(&wideLineFragShader);
		linkProg(wideLineShaderProgram, "wideLineShaderProgram");
		wideLineShaderVars.projectionMatrix = wideLineShaderProgram->uniformLocation("projectionMatrix");
		wideLineShaderVars.viewportSize     = wideLineShaderProgram->uniformLocation("viewportSize");
		wideLineShaderVars.lineWidth        = wideLineShaderProgram->uniformLocation("lineWidth");
		wideLineShaderVars.color            = wideLineShaderProgram->uniformLocation("color");
		wideLineShaderVars.vertex = wideLineShaderProgram->attributeLocation("vertex");

		// Now a version of the shader that supports color interpolated along the line
		QOpenGLShader colorfulWideLineVertShader(QOpenGLShader::Vertex);
		colorfulWideLineVertShader.compileSourceCode(1+R"(
#version 330
in vec3 vertex;
in vec4 color;
out vec4 varyingColor;
uniform mat4 projectionMatrix;
void main()
{
    gl_Position = projectionMatrix*vec4(vertex, 1.);
	varyingColor = color;
}
)");
		if (!colorfulWideLineVertShader.log().isEmpty())
			qWarning().noquote() << "StelPainter: Warnings while compiling colorful wide line vertex shader: " << colorfulWideLineVertShader.log();

		QOpenGLShader colorfulWideLineGeomShader(QOpenGLShader::Geometry);
		colorfulWideLineGeomShader.compileSourceCode(QByteArray(geomSrc).replace("@VARYING_COLOR@","1"));
		if (!colorfulWideLineGeomShader.log().isEmpty())
			qWarning().noquote() << "StelPainter: Warnings while compiling colorful wide line geometry shader: " << colorfulWideLineGeomShader.log();

		QOpenGLShader colorfulWideLineFragShader(QOpenGLShader::Fragment);
		colorfulWideLineFragShader.compileSourceCode(1+R"(
#version 330
in vec4 geomColor;
out vec4 outputColor;
void main()
{
    outputColor = geomColor;
}
)");
		if (!colorfulWideLineFragShader.log().isEmpty())
			qWarning().noquote() << "StelPainter: Warnings while compiling colorful wide line fragment shader: " << colorfulWideLineFragShader.log();
		colorfulWideLineShaderProgram = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
		colorfulWideLineShaderProgram->addShader(&colorfulWideLineVertShader);
		colorfulWideLineShaderProgram->addShader(&colorfulWideLineGeomShader);
		colorfulWideLineShaderProgram->addShader(&colorfulWideLineFragShader);
		linkProg(colorfulWideLineShaderProgram, "colorfulWideLineShaderProgram");
		colorfulWideLineShaderVars.projectionMatrix = colorfulWideLineShaderProgram->uniformLocation("projectionMatrix");
		colorfulWideLineShaderVars.viewportSize     = colorfulWideLineShaderProgram->uniformLocation("viewportSize");
		colorfulWideLineShaderVars.lineWidth        = colorfulWideLineShaderProgram->uniformLocation("lineWidth");
		colorfulWideLineShaderVars.color  = colorfulWideLineShaderProgram->attributeLocation("color");
		colorfulWideLineShaderVars.vertex = colorfulWideLineShaderProgram->attributeLocation("vertex");
	}

	multisamplingEnabled = StelApp::getInstance().getSettings()->value("video/multisampling", 0).toUInt() != 0;
}


void StelPainter::deinitGLShaders()
{
	delete basicShaderProgram;
	basicShaderProgram = Q_NULLPTR;
	delete colorShaderProgram;
	colorShaderProgram = Q_NULLPTR;
	delete texturesShaderProgram;
	texturesShaderProgram = Q_NULLPTR;
	delete texturesColorShaderProgram;
	texturesColorShaderProgram = Q_NULLPTR;
	delete wideLineShaderProgram;
	wideLineShaderProgram = Q_NULLPTR;
	delete colorfulWideLineShaderProgram;
	colorfulWideLineShaderProgram = Q_NULLPTR;
	texCache.clear();
}


void StelPainter::setArrays(const Vec3d* vertices, const Vec2f* texCoords, const Vec3f* colorArray, const Vec3f* normalArray)
{
	enableClientStates(vertices, texCoords, colorArray, normalArray);
	setVertexPointer(3, GL_DOUBLE, vertices);
	setTexCoordPointer(2, GL_FLOAT, texCoords);
	setColorPointer(3, GL_FLOAT, colorArray);
	setNormalPointer(GL_FLOAT, normalArray);
}

void StelPainter::setArrays(const Vec3f* vertices, const Vec2f* texCoords, const Vec3f* colorArray, const Vec3f* normalArray)
{
	enableClientStates(vertices, texCoords, colorArray, normalArray);
	setVertexPointer(3, GL_FLOAT, vertices);
	setTexCoordPointer(2, GL_FLOAT, texCoords);
	setColorPointer(3, GL_FLOAT, colorArray);
	setNormalPointer(GL_FLOAT, normalArray);
}

void StelPainter::enableClientStates(bool vertex, bool texture, bool color, bool normal)
{
	vertexArray.enabled = vertex;
	texCoordArray.enabled = texture;
	colorArray.enabled = color;
	normalArray.enabled = normal;
}

void StelPainter::drawFixedColorWideLinesAsQuads(const ArrayDesc& vertexArray, int count, int offset,
													 const Mat4f& projMat, const DrawingMode mode)
{
	if(count < 2) return;

	GLint viewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, viewport);
	const auto viewportSize = Vec2f(viewport[2], viewport[3]);

	std::vector<Vec4f> newVertices;
	if(mode == LineStrip)
		newVertices.reserve((count-1)*6);
	else
		newVertices.reserve(count*6);
	newVertices.resize((count-1)*6);
	Q_ASSERT(vertexArray.type == GL_FLOAT);
	const auto lineVertData = static_cast<const float*>(vertexArray.pointer) + vertexArray.size*offset;
	const int step = mode==Lines ? 2 : 1;
	const bool connected = mode!=Lines;
	Vec2f prevV1aNDC, prevV1bNDC, prevLineDirNDC;
	for(int n = 0; n < count-1; n += step)
	{
		Vec4f in0;
		Vec4f in1;
		switch(vertexArray.size)
		{
		case 4:
			in0 = Vec4f(lineVertData+4*(n+0));
			in1 = Vec4f(lineVertData+4*(n+1));
			break;
		case 3:
			in0 = Vec4f(lineVertData[3*(n+0)], lineVertData[3*(n+0)+1], lineVertData[3*(n+0)+2], 1);
			in1 = Vec4f(lineVertData[3*(n+1)], lineVertData[3*(n+1)+1], lineVertData[3*(n+1)+2], 1);
			break;
		case 2:
			in0 = Vec4f(lineVertData[2*(n+0)], lineVertData[2*(n+0)+1], 0, 1);
			in1 = Vec4f(lineVertData[2*(n+1)], lineVertData[2*(n+1)+1], 0, 1);
			break;
		case 1:
			in0 = Vec4f(lineVertData[n+0], 0,0,1);
			in1 = Vec4f(lineVertData[n+1], 0,0,1);
			break;
		default:
			in0 = Vec4f(0.f);
			in1 = Vec4f(0.f);
			qCritical("Bad number of elements in vertex array"); // or qFatal()?
		}

		const Vec4f clip0 = projMat * Vec4f(in0);
		const Vec4f clip1 = projMat * Vec4f(in1);

		const Vec3f ndc0 = Vec3f(clip0.v) / clip0[3];
		const Vec3f ndc1 = Vec3f(clip1.v) / clip1[3];

		const Vec2f lineDirNDC = normalize(Vec2f(ndc1.v) - Vec2f(ndc0.v));
		const Vec2f lineDir2d = normalize(lineDirNDC * viewportSize);
		const Vec2f perpendicularDir2d = Vec2f(-lineDir2d[1], lineDir2d[0]);

		const Vec2f offset2d = (Vec2f(glState.lineWidth) / viewportSize) * perpendicularDir2d;

		// 2D NDC of the new pairs (a,b) of vertices for each input vertex (0,1)
		      Vec2f v0aNDC = Vec2f(ndc0.v) + offset2d;
		      Vec2f v0bNDC = Vec2f(ndc0.v) - offset2d;
		const Vec2f v1aNDC = Vec2f(ndc1.v) + offset2d;
		const Vec2f v1bNDC = Vec2f(ndc1.v) - offset2d;

		if(connected && n > 0) // at n==0 we initialize prevV1{a,b}XY
		{
			const auto processLineSide = [lineDirNDC,prevLineDirNDC](const Vec2f& prevV1, Vec2f& currV0) -> bool
			{
				// Find prevT and currT for which prevV1+prevLineDirNDC*prevT==currV0+lineDirNDC*currT.
				// This will define the intersection point of the line side.
				const float prevT = (lineDirNDC[1]*(prevV1[0]-currV0[0]) + lineDirNDC[0]*(currV0[1]-prevV1[1]))
				                                                               /
				                               (lineDirNDC[0]*prevLineDirNDC[1] - lineDirNDC[1]*prevLineDirNDC[0]);
				const float currT = (prevLineDirNDC[1]*(prevV1[0]-currV0[0]) + prevLineDirNDC[0]*(currV0[1]-prevV1[1]))
				                                                               /
				                               (lineDirNDC[0]*prevLineDirNDC[1] - lineDirNDC[1]*prevLineDirNDC[0]);
				if(prevT > 0 && currT < 0)
				{
					// This is the outer corner of a joint. Extrapolate beginning
					// of the current line back to the intersection.
					currV0 += lineDirNDC*currT;
					return true;
				}
				return false;
			};

			// Process one side of the line (we call it A)
			if(processLineSide(prevV1aNDC, v0aNDC))
			{
				// Extrapolate the previous line end forward to the intersection
				newVertices[6*(n-1)+2][0] = v0aNDC[0]*clip0[3];
				newVertices[6*(n-1)+2][1] = v0aNDC[1]*clip0[3];
				newVertices[6*(n-1)+3][0] = v0aNDC[0]*clip0[3];
				newVertices[6*(n-1)+3][1] = v0aNDC[1]*clip0[3];
			}

			// Process the second side of the line (we call it B)
			if(processLineSide(prevV1bNDC, v0bNDC))
			{
				// Extrapolate the previous line end forward to the intersection
				newVertices[6*(n-1)+5][0] = v0bNDC[0]*clip0[3];
				newVertices[6*(n-1)+5][1] = v0bNDC[1]*clip0[3];
			}
		}
		prevV1aNDC = v1aNDC;
		prevV1bNDC = v1bNDC;
		prevLineDirNDC = lineDirNDC;

		// 2D clip space coordinates of the new pairs (a,b) of vertices for each input vertex (0,1)
		const Vec2f v0a_xy = v0aNDC*clip0[3];
		const Vec2f v0b_xy = v0bNDC*clip0[3];
		const Vec2f v1a_xy = v1aNDC*clip1[3];
		const Vec2f v1b_xy = v1bNDC*clip1[3];

		// Final 4D coordinates of the new vertices
		const auto v0a = Vec4f(v0a_xy[0], v0a_xy[1], clip0[2], clip0[3]);
		const auto v0b = Vec4f(v0b_xy[0], v0b_xy[1], clip0[2], clip0[3]);
		const auto v1a = Vec4f(v1a_xy[0], v1a_xy[1], clip1[2], clip1[3]);
		const auto v1b = Vec4f(v1b_xy[0], v1b_xy[1], clip1[2], clip1[3]);

		newVertices[6*n+0] = v0a;
		newVertices[6*n+1] = v0b;
		newVertices[6*n+2] = v1a;

		newVertices[6*n+3] = v1a;
		newVertices[6*n+4] = v0b;
		newVertices[6*n+5] = v1b;
	}
	if(mode == LineLoop)
	{
		// Connect the ends
		const auto lastN = newVertices.size()-1;
		Q_ASSERT(lastN >= 2);
		newVertices.push_back(newVertices[lastN-2]);
		newVertices.push_back(newVertices[lastN]);
		newVertices.push_back(newVertices[0]);

		newVertices.push_back(newVertices[0]);
		newVertices.push_back(newVertices[lastN]);
		newVertices.push_back(newVertices[1]);
	}
	vao->bind();
	verticesVBO->bind();

	verticesVBO->allocate(newVertices.data(), newVertices.size() * sizeof newVertices[0]);

	const auto& pr = basicShaderProgram;
	pr->bind();
	pr->setAttributeBuffer(basicShaderVars.vertex, vertexArray.type, 0, 4);
	pr->enableAttributeArray(basicShaderVars.vertex);
	pr->setUniformValue(basicShaderVars.projectionMatrix, QMatrix4x4{});
	pr->setUniformValue(basicShaderVars.color, currentColor.toQVector());

#ifdef GL_MULTISAMPLE
	const bool multisampleWasOn = multisamplingEnabled && glIsEnabled(GL_MULTISAMPLE);
	if(multisamplingEnabled && glState.lineSmooth)
		glEnable(GL_MULTISAMPLE);
#endif

	// TODO: convert this to a triangle strip, but mind the overlaps and gaps of non-parallel lines' corners
	glDrawArrays(GL_TRIANGLES, 0, newVertices.size());

	verticesVBO->release();
	vao->release();

	if (pr) pr->release();

#ifdef GL_MULTISAMPLE
	if(multisamplingEnabled && !multisampleWasOn && glState.lineSmooth)
		glDisable(GL_MULTISAMPLE);
#endif
}

void StelPainter::drawFromArray(DrawingMode mode, int count, int offset, bool doProj, const unsigned short* indices)
{
	if(!count) return;

	ArrayDesc projectedVertexArray = vertexArray;
	if (doProj)
	{
		// Project the vertex array using current projection
		if (indices)
			projectedVertexArray = projectArray(vertexArray, 0, count, indices + offset);
		else
			projectedVertexArray = projectArray(vertexArray, offset, count, Q_NULLPTR);
	}

	QOpenGLShaderProgram* pr=Q_NULLPTR;

	const Mat4f& m = getProjector()->getProjectionMatrix();
	const QMatrix4x4 qMat(m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14], m[3], m[7], m[11], m[15]);

	const bool lineMode = mode==LineStrip || mode==LineLoop || mode==Lines;
	const bool isCoreProfile = StelMainView::getInstance().getGLInformation().isCoreProfile;
	const bool isGLES = StelMainView::getInstance().getGLInformation().isGLES;
	const bool wideLineMode = lineMode && glState.lineWidth>1;
	if(wideLineMode && (isCoreProfile || isGLES) && projectedVertexArray.type == GL_FLOAT)
	{
		const bool fixedColor = !texCoordArray.enabled && !colorArray.enabled && !normalArray.enabled;
		if(fixedColor && !indices)
		{
			// Optimized version that doesn't use geometry shader, which appears to be slow on some GPUs.
			// Ideally, we'd like to get rid of the geometry shader completely,
			// but this will result in more code for small performance gain.
			drawFixedColorWideLinesAsQuads(projectedVertexArray, count, offset, m, mode);
			return;
		}
	}
	const bool coreProfileWideLineMode = wideLineMode && isCoreProfile;

	vao->bind();
	verticesVBO->bind();
	GLsizeiptr numberOfVerticesToCopy = count + offset;
	if(indices)
	{
		indicesVBO->bind();
		indicesVBO->allocate(indices+offset, count * sizeof indices[0]);
		numberOfVerticesToCopy = 1 + *std::max_element(indices + offset, indices + offset + count);
	}

#ifdef GL_MULTISAMPLE
	const bool multisampleWasOn = multisamplingEnabled && glIsEnabled(GL_MULTISAMPLE);
#endif

	if (!texCoordArray.enabled && !colorArray.enabled && !normalArray.enabled)
	{
		pr = coreProfileWideLineMode ? wideLineShaderProgram : basicShaderProgram;
		pr->bind();

		verticesVBO->allocate(projectedVertexArray.pointer, projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy);

#ifdef GL_MULTISAMPLE
		if(multisamplingEnabled && glState.lineSmooth)
			glEnable(GL_MULTISAMPLE);
#endif
		if(coreProfileWideLineMode)
		{
			pr->setAttributeBuffer(wideLineShaderVars.vertex, projectedVertexArray.type, 0, projectedVertexArray.size);
			pr->enableAttributeArray(wideLineShaderVars.vertex);
			pr->setUniformValue(wideLineShaderVars.projectionMatrix, qMat);
			pr->setUniformValue(wideLineShaderVars.color, currentColor.toQVector());
			pr->setUniformValue(wideLineShaderVars.lineWidth, glState.lineWidth);
			GLint viewport[4] = {};
			glGetIntegerv(GL_VIEWPORT, viewport);
			pr->setUniformValue(wideLineShaderVars.viewportSize, QVector2D(viewport[2], viewport[3]));
		}
		else
		{
			pr->setAttributeBuffer(basicShaderVars.vertex, projectedVertexArray.type, 0, projectedVertexArray.size);
			pr->enableAttributeArray(basicShaderVars.vertex);
			pr->setUniformValue(basicShaderVars.projectionMatrix, qMat);
			pr->setUniformValue(basicShaderVars.color, currentColor.toQVector());
		}
	}
	else if (texCoordArray.enabled && !colorArray.enabled && !normalArray.enabled && !wideLineMode)
	{
		pr = texturesShaderProgram;
		pr->bind();

		const auto bufferSize = projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy +
								texCoordArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->allocate(bufferSize);
		const auto vertexDataSize = projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(0, projectedVertexArray.pointer, vertexDataSize);
		const auto texCoordDataOffset = vertexDataSize;
		const auto texCoordDataSize = texCoordArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(texCoordDataOffset, texCoordArray.pointer, texCoordDataSize);

		pr->setAttributeBuffer(texturesShaderVars.vertex, projectedVertexArray.type, 0, projectedVertexArray.size);
		pr->enableAttributeArray(texturesShaderVars.vertex);
		pr->setUniformValue(texturesShaderVars.projectionMatrix, qMat);
		pr->setUniformValue(texturesShaderVars.texColor, currentColor.toQVector());
		pr->setAttributeBuffer(texturesShaderVars.texCoord, texCoordArray.type, texCoordDataOffset, texCoordArray.size);
		pr->enableAttributeArray(texturesShaderVars.texCoord);
		//pr->setUniformValue(texturesShaderVars.texture, 0);    // use texture unit 0
	}
	else if (texCoordArray.enabled && colorArray.enabled && !normalArray.enabled && !wideLineMode)
	{
		pr = texturesColorShaderProgram;
		pr->bind();

		const auto bufferSize = projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy +
								texCoordArray.vertexSizeInBytes()*numberOfVerticesToCopy +
								colorArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->allocate(bufferSize);
		const auto vertexDataSize = projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(0, projectedVertexArray.pointer, vertexDataSize);
		const auto texCoordDataOffset = vertexDataSize;
		const auto texCoordDataSize = texCoordArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(texCoordDataOffset, texCoordArray.pointer, texCoordDataSize);
		const auto colorDataOffset = vertexDataSize + texCoordDataSize;
		const auto colorDataSize = colorArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(colorDataOffset, colorArray.pointer, colorDataSize);

		pr->setAttributeBuffer(texturesColorShaderVars.vertex, projectedVertexArray.type, 0, projectedVertexArray.size);
		pr->enableAttributeArray(texturesColorShaderVars.vertex);
		pr->setUniformValue(texturesColorShaderVars.projectionMatrix, qMat);
		pr->setAttributeBuffer(texturesColorShaderVars.texCoord, texCoordArray.type, texCoordDataOffset, texCoordArray.size);
		pr->enableAttributeArray(texturesColorShaderVars.texCoord);
		pr->setAttributeBuffer(texturesColorShaderVars.color, colorArray.type, colorDataOffset, colorArray.size);
		pr->enableAttributeArray(texturesColorShaderVars.color);
		//pr->setUniformValue(texturesShaderVars.texture, 0);    // use texture unit 0
		pr->setUniformValue(texturesColorShaderVars.saturation, saturation);
	}
	else if (!texCoordArray.enabled && colorArray.enabled && !normalArray.enabled)
	{
		pr = coreProfileWideLineMode ? colorfulWideLineShaderProgram : colorShaderProgram;
		pr->bind();

		const auto bufferSize = projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy +
								colorArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->allocate(bufferSize);
		const auto vertexDataSize = projectedVertexArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(0, projectedVertexArray.pointer, vertexDataSize);
		const auto colorDataOffset = vertexDataSize;
		const auto colorDataSize = colorArray.vertexSizeInBytes()*numberOfVerticesToCopy;
		verticesVBO->write(colorDataOffset, colorArray.pointer, colorDataSize);

		if(coreProfileWideLineMode)
		{
			pr->setAttributeBuffer(colorfulWideLineShaderVars.vertex, projectedVertexArray.type, 0, projectedVertexArray.size);
			pr->enableAttributeArray(colorfulWideLineShaderVars.vertex);
			pr->setAttributeBuffer(colorfulWideLineShaderVars.color, colorArray.type, colorDataOffset, colorArray.size);
			pr->enableAttributeArray(colorfulWideLineShaderVars.color);
			pr->setUniformValue(colorfulWideLineShaderVars.projectionMatrix, qMat);
			pr->setUniformValue(colorfulWideLineShaderVars.lineWidth, glState.lineWidth);
			GLint viewport[4] = {};
			glGetIntegerv(GL_VIEWPORT, viewport);
			pr->setUniformValue(colorfulWideLineShaderVars.viewportSize, QVector2D(viewport[2], viewport[3]));
		}
		else
		{
			pr->setAttributeBuffer(colorShaderVars.vertex, projectedVertexArray.type, 0, projectedVertexArray.size);
			pr->enableAttributeArray(colorShaderVars.vertex);
			pr->setUniformValue(colorShaderVars.projectionMatrix, qMat);
			pr->setAttributeBuffer(colorShaderVars.color, colorArray.type, colorDataOffset, colorArray.size);
			pr->enableAttributeArray(colorShaderVars.color);
		}
#ifdef GL_MULTISAMPLE
		if(multisamplingEnabled && glState.lineSmooth)
			glEnable(GL_MULTISAMPLE);
#endif
	}
	else
	{
		qDebug() << "Unhandled parameters." << texCoordArray.enabled << colorArray.enabled << normalArray.enabled << wideLineMode;
		Q_ASSERT(0);
		return;
	}
	
	if (indices)
		glDrawElements(mode, count, GL_UNSIGNED_SHORT, 0);
	else
		glDrawArrays(mode, offset, count);

	verticesVBO->release();
	indicesVBO->release();
	vao->release();

	if (pr)
		pr->release();

#ifdef GL_MULTISAMPLE
	if(multisamplingEnabled && !multisampleWasOn && wideLineMode && glState.lineSmooth)
		glDisable(GL_MULTISAMPLE);
#endif
}

size_t StelPainter::ArrayDesc::vertexSizeInBytes() const
{
	size_t elemSize = 1;
	switch(type)
	{
	case GL_SHORT:
		elemSize = sizeof(GLshort);
		break;
	case GL_INT:
		elemSize = sizeof(GLint);
		break;
	case GL_FLOAT:
		elemSize = sizeof(GLfloat);
		break;
#if GL_DOUBLE != GL_FLOAT // see StelOpenGL.hpp
	case GL_DOUBLE:
		elemSize = sizeof(GLdouble);
		break;
#endif
	default:
		Q_ASSERT_X(false, Q_FUNC_INFO, "Unexpected element type");
		break;
	}
	return elemSize * size;
}

StelPainter::ArrayDesc StelPainter::projectArray(const StelPainter::ArrayDesc& array, int offset, int count, const unsigned short* indices)
{
	// XXX: we should use a more generic way to test whether or not to do the projection.
	if (dynamic_cast<StelProjector2d*>(prj.data()))
	{
		return array;
	}

	Q_ASSERT(array.size == 3);
	Q_ASSERT(array.type == GL_DOUBLE);
	const Vec3d* vecArray = reinterpret_cast<const Vec3d *>(const_cast<void*>(array.pointer));

	// We have two different cases :
	// 1) We are not using an indice array.  In that case the size of the array is known
	// 2) We are using an indice array.  In that case we have to find the max value by iterating through the indices.
	if (!indices)
	{
		polygonVertexArray.resize(offset + count);
		prj->project(count, vecArray + offset, polygonVertexArray.data() + offset);
	} else
	{
		// we need to find the max value of the indices !
		unsigned short max = 0;
		for (int i = offset; i < offset + count; ++i)
		{
			max = std::max(max, indices[i]);
		}
		polygonVertexArray.resize(max+1);
		prj->project(max + 1, vecArray + offset, polygonVertexArray.data() + offset);
	}

	ArrayDesc ret;
	ret.size = 3;
	ret.type = GL_FLOAT;
	ret.pointer = polygonVertexArray.constData();
	ret.enabled = array.enabled;
	return ret;
}

