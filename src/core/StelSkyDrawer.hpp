/*
 * Stellarium
 * Copyright (C) 2007 Fabien Chereau
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

#ifndef STELSKYDRAWER_HPP
#define STELSKYDRAWER_HPP

#include "StelApp.hpp"
#include "RefractionExtinction.hpp"
#include "StelTextureTypes.hpp"
#include "VecMath.hpp"

#include <memory>
#include <QObject>
#include <QImage>
#include <QSettings>
#include <QOpenGLFunctions>

class QOpenGLVertexArrayObject;
class StelToneReproducer;
class QOpenGLBuffer;
class StelCore;
class StelPainter;

//! Contains the 2 parameters necessary to draw a star on screen.
//! the radius and luminance of the star halo texture.
struct RCMag
{
	float radius;
	float luminance;
};

//! @class StelSkyDrawer
//! Provide a set of methods used to draw sky objects taking into account
//! eyes adaptation, zoom level, instrument model and artificially set magnitude limits
class StelSkyDrawer : public QObject, protected QOpenGLFunctions
{
	Q_OBJECT

	//! Sets how much brighter stars will be bigger than fainter stars
	Q_PROPERTY(double relativeStarScale READ getRelativeStarScale WRITE setRelativeStarScale NOTIFY relativeStarScaleChanged)
	//! The absolute star brightness scale
	Q_PROPERTY(double absoluteStarScale READ getAbsoluteStarScale WRITE setAbsoluteStarScale NOTIFY absoluteStarScaleChanged)
	Q_PROPERTY(double twinkleAmount READ getTwinkleAmount WRITE setTwinkleAmount NOTIFY twinkleAmountChanged)
	Q_PROPERTY(bool flagStarTwinkle READ getFlagTwinkle WRITE setFlagTwinkle NOTIFY flagTwinkleChanged)
	Q_PROPERTY(bool flagForcedTwinkle READ getFlagForcedTwinkle WRITE setFlagForcedTwinkle NOTIFY flagForcedTwinkleChanged)
	Q_PROPERTY(double lightPollutionLuminance READ getLightPollutionLuminance WRITE setLightPollutionLuminance NOTIFY lightPollutionLuminanceChanged)
	Q_PROPERTY(bool flagDrawBigStarHalo READ getFlagDrawBigStarHalo WRITE setFlagDrawBigStarHalo NOTIFY flagDrawBigStarHaloChanged)
	Q_PROPERTY(bool flagStarSpiky READ getFlagStarSpiky WRITE setFlagStarSpiky NOTIFY flagStarSpikyChanged)

	Q_PROPERTY(bool flagStarMagnitudeLimit READ getFlagStarMagnitudeLimit WRITE setFlagStarMagnitudeLimit NOTIFY flagStarMagnitudeLimitChanged)
	Q_PROPERTY(bool flagNebulaMagnitudeLimit READ getFlagNebulaMagnitudeLimit WRITE setFlagNebulaMagnitudeLimit NOTIFY flagNebulaMagnitudeLimitChanged)
	Q_PROPERTY(bool flagPlanetMagnitudeLimit READ getFlagPlanetMagnitudeLimit WRITE setFlagPlanetMagnitudeLimit NOTIFY flagPlanetMagnitudeLimitChanged)

	Q_PROPERTY(double customStarMagLimit READ getCustomStarMagnitudeLimit WRITE setCustomStarMagnitudeLimit NOTIFY customStarMagLimitChanged)
	Q_PROPERTY(double customNebulaMagLimit READ getCustomNebulaMagnitudeLimit WRITE setCustomNebulaMagnitudeLimit NOTIFY customNebulaMagLimitChanged)
	Q_PROPERTY(double customPlanetMagLimit READ getCustomPlanetMagnitudeLimit WRITE setCustomPlanetMagnitudeLimit NOTIFY customPlanetMagLimitChanged)

	Q_PROPERTY(bool flagLuminanceAdaptation READ getFlagLuminanceAdaptation WRITE setFlagLuminanceAdaptation NOTIFY flagLuminanceAdaptationChanged)
	Q_PROPERTY(double daylightLabelThreshold READ getDaylightLabelThreshold WRITE setDaylightLabelThreshold NOTIFY daylightLabelThresholdChanged)

	Q_PROPERTY(double extinctionCoefficient READ getExtinctionCoefficient WRITE setExtinctionCoefficient NOTIFY extinctionCoefficientChanged)
	Q_PROPERTY(double atmosphereTemperature READ getAtmosphereTemperature WRITE setAtmosphereTemperature NOTIFY atmosphereTemperatureChanged)
	Q_PROPERTY(double atmospherePressure READ getAtmospherePressure WRITE setAtmospherePressure NOTIFY atmospherePressureChanged)

	Q_PROPERTY(bool flagDrawSunAfterAtmosphere READ getFlagDrawSunAfterAtmosphere WRITE setFlagDrawSunAfterAtmosphere NOTIFY flagDrawSunAfterAtmosphereChanged)
	Q_PROPERTY(bool flagEarlySunHalo READ getFlagEarlySunHalo WRITE setFlagEarlySunHalo NOTIFY flagEarlySunHaloChanged)
	Q_PROPERTY(bool flagTfromK READ getFlagTfromK WRITE setFlagTfromK NOTIFY flagTfromKChanged)
	Q_PROPERTY(double turbidity READ getT WRITE setT NOTIFY turbidityChanged)

public:
	//! Constructor
	StelSkyDrawer(StelCore* core);
	//! Destructor
	~StelSkyDrawer() override;

	//! Init parameters from config file
	void init();

	//! Update with respect to the time and StelProjector/StelToneReproducer state
	//! @param deltaTime the time increment in second since last call.
	void update(double deltaTime);

	//! Set the proper openGL state before making calls to drawPointSource
	//! @param p a pointer to a valid instance of a Painter. The instance must be valid until postDrawPointSource() is called
	void preDrawPointSource(StelPainter* p);

	//! Finalize the drawing of point sources
	void postDrawPointSource(StelPainter* sPainter);

	//! Draw a point source halo.
	//! @param sPainter the StelPainter to use for drawing.
	//! @param v the 3d position of the source in J2000 reference frame
	//! @param rcMag the radius and luminance of the source as computed by computeRCMag()
	//! @param bVindex the source B-V index (into the private colorTable. This is not the astronomical B-V value.)
	//! @param checkInScreen whether source in screen should be checked to avoid unnecessary drawing.
	//! @param twinkleFactor allows height-dependent twinkling. Recommended value: min(1,1-0.9*sin(altitude)). Allowed values [0..1]
	//! @return true if the source was actually visible and drawn
	bool drawPointSource(StelPainter* sPainter, const Vec3d& v, const RCMag &rcMag, int bVindex, bool checkInScreen=false, float twinkleFactor=1.0f)
	{
		return drawPointSource(sPainter, v, rcMag, colorTable[bVindex], checkInScreen, twinkleFactor);
	}

	//! Draw a point source halo.
	//! @param sPainter the StelPainter to use for drawing.
	//! @param v the 3d position of the source in J2000 reference frame
	//! @param rcMag the radius and luminance of the source as computed by computeRCMag()
	//! @param color the RGB color value (0...1, 0...1, 0...1)
	//! @param checkInScreen whether source in screen should be checked to avoid unnecessary drawing.
	//! @param twinkleFactor allows height-dependent twinkling. Recommended value: min(1,1-0.9*sin(altitude)). Allowed values [0..1]
	//! @return true if the source was actually visible and drawn
	bool drawPointSource(StelPainter* sPainter, const Vec3d& v, const RCMag &rcMag, const Vec3f& bcolor, bool checkInScreen=false, float twinkleFactor=1.0f);

	//! Draw an image of the solar corona onto the screen at position v.
	//! @param radius depends on the actually used texture and current disk size of the sun.
	//! @param alpha opacity value. Set 1 for full visibility, but usually keep close to 0 except during solar eclipses.
	//! @param angle includes parallactic angle (if alt/azimuth frame) and angle between solar polar axis and celestial equator.
	void drawSunCorona(StelPainter* painter, const Vec3f& v, float radius, const Vec3f& color, const float alpha, const float angle);

	//! Terminate drawing of a 3D model, draw the halo
	//! @param p the StelPainter instance to use for this drawing operation
	//! @param v the 3d position of the source in J2000 reference frame
	//! @param illuminatedArea the illuminated area in arcmin^2
	//! @param mag the source integrated magnitude
	//! @param color the object halo RGB color
	//! @param isSun the object is the sun (will be drawn with different texture)
	void postDrawSky3dModel(StelPainter* p, const Vec3d& v, float illuminatedArea, float mag, const Vec3f& color = Vec3f(1.f,1.f,1.f), const bool isSun=false);

	//! Compute RMag and CMag from magnitude.
	//! @param mag the object integrated V magnitude
	//! @param rcMag array of 2 floats containing the radius and luminance
	//! @return false if the object is too faint to be displayed
	bool computeRCMag(float mag, RCMag*) const;

	//! Report that an object of luminance lum with an on-screen area of area pixels is currently displayed
	//! This information is used to determine the world adaptation luminance
	//! This method should be called during the update operations of the main loop
	//! @param lum luminance in cd/m^2
	//! @param fastAdaptation adapt the eye quickly if true, other wise use a smooth adaptation
	void reportLuminanceInFov(float lum, bool fastAdaptation=false);

	//! To be called before the drawing stage starts
	void preDraw();

	//! Compute the luminance for an extended source with the given surface brightness
	//! @param sb surface brightness in V magnitude/arcmin^2
	//! @return the luminance in cd/m^2
	static float surfaceBrightnessToLuminance(float sb);
	//! Compute the surface brightness from the luminance of an extended source
	//! @param lum luminance in cd/m^2
	//! @return surface brightness in V magnitude/arcmin^2
	static float luminanceToSurfacebrightness(float lum);

	//! Convert quantized B-V index to float B-V
	static inline float indexToBV(unsigned char bV)
	{
		return static_cast<float>(bV)*(4.f/127.f)-0.5f;
	}

	//! Convert quantized B-V index to RGB colors
	static inline const Vec3f& indexToColor(int bV)
	{
		return colorTable[bV];
	}

public slots:
	//! Set the way brighter stars will look bigger as the fainter ones
	void setRelativeStarScale(double b=1.0);
	//! Get the way brighter stars will look bigger as the fainter ones
	double getRelativeStarScale() const {return starRelativeScale;}

	//! Set the absolute star brightness scale
	void setAbsoluteStarScale(double b=1.0);
	//! Get the absolute star brightness scale
	double getAbsoluteStarScale() const {return starAbsoluteScaleF;}

	//! Set source twinkle amount.
	void setTwinkleAmount(double b);
	//! Get source twinkle amount.
	double getTwinkleAmount() const {return twinkleAmount;}

	//! Set flag for source twinkling.
	void setFlagTwinkle(bool b);
	//! Get flag for source twinkling.
	bool getFlagTwinkle() const {return flagStarTwinkle;}

	//! Set flag for enable twinkling of stars without atmosphere.
	//! @note option for planetariums
	void setFlagForcedTwinkle(bool b);
	//! Get flag for enable twinkling of stars without atmosphere.
	//! @note option for planetariums
	bool getFlagForcedTwinkle() const {return flagForcedTwinkle;}

	//! Set the parameters so that the stars disappear at about the naked-eye limiting magnitude corresponding
	//! to the given zenith luminance at moonless night.
	//! The limit is valid only at a given zoom level (around 60 deg)
	void setLightPollutionLuminance(double luminance);
	//! Get the current zenith luminance at moonless night.
	double getLightPollutionLuminance() const {return lightPollutionLuminance;}

	Q_DECL_DEPRECATED int getBortleScaleIndex() const;

	//! Set flag for drawing a halo around bright stars.
	void setFlagDrawBigStarHalo(bool b);
	//! Get flag for drawing a halo around bright stars.
	bool getFlagDrawBigStarHalo() const {return flagDrawBigStarHalo;}

	//! Set flag to draw stars with rays
	void setFlagStarSpiky(bool b);
	//! Get whether to draw stars with rays
	bool getFlagStarSpiky() const {return flagStarSpiky;}

	//! Get the magnitude of the currently faintest visible point source
	//! It depends on the zoom level, on the eye adaptation and on the point source rendering parameters
	//! @return the limit V mag at which a point source will be displayed
	float getLimitMagnitude() const {return limitMagnitude;}

	//! Toggle the application of user-defined star magnitude limit.
	//! If enabled, stars fainter than the magnitude set with
	//! setCustomStarMagnitudeLimit() will not be displayed.
	void setFlagStarMagnitudeLimit(bool b);
	//! @return true if the user-defined star magnitude limit is in force.
	bool getFlagStarMagnitudeLimit() const {return flagStarMagnitudeLimit;}
	//! Toggle the application of user-defined deep-sky object magnitude limit.
	//! If enabled, deep-sky objects fainter than the magnitude set with
	//! setCustomNebulaMagnitudeLimit() will not be displayed.
	void setFlagNebulaMagnitudeLimit(bool b);
	//! @return true if the user-defined nebula magnitude limit is in force.
	bool getFlagNebulaMagnitudeLimit() const {return flagNebulaMagnitudeLimit;}
	//! Toggle the application of user-defined solar system object magnitude limit.
	//! If enabled, planets, planetary moons, asteroids (KBO, ...) and comets fainter than the magnitude set with
	//! setCustomPlanetMagnitudeLimit() will not be displayed.
	void setFlagPlanetMagnitudeLimit(bool b);
	//! @return true if the user-defined nebula magnitude limit is in force.
	bool getFlagPlanetMagnitudeLimit() const {return flagPlanetMagnitudeLimit;}

	//! Get the value used for forced star magnitude limiting.
	double getCustomStarMagnitudeLimit() const {return customStarMagLimit;}
	//! Sets a lower limit for star magnitudes (anything fainter is ignored).
	//! In force only if flagStarMagnitudeLimit is set.
	void setCustomStarMagnitudeLimit(double limit);
	//! Get the value used for forced nebula magnitude limiting.
	double getCustomNebulaMagnitudeLimit() const {return customNebulaMagLimit;}
	//! Sets a lower limit for nebula magnitudes (anything fainter is ignored).
	//! In force only if flagNebulaMagnitudeLimit is set.
	void setCustomNebulaMagnitudeLimit(double limit);
	//! Get the value used for forced solar system object magnitude limiting.
	double getCustomPlanetMagnitudeLimit() const {return customPlanetMagLimit;}
	//! Sets a lower limit for solar system object magnitudes (anything fainter is ignored).
	//! In force only if flagPlanetMagnitudeLimit is set.
	void setCustomPlanetMagnitudeLimit(double limit);

	//! Get the luminance of the faintest visible object (e.g. RGB<0.05)
	//! It depends on the zoom level, on the eye adaptation and on the point source rendering parameters
	//! @return the limit V luminance at which an object will be visible
	float getLimitLuminance() const {return limitLuminance;}

	//! Set the value of the eye adaptation flag
	void setFlagLuminanceAdaptation(bool b);
	//! Get the current value of eye adaptation flag
	bool getFlagLuminanceAdaptation() const {return flagLuminanceAdaptation;}

	//! Set the label brightness threshold
	void setDaylightLabelThreshold(double t);
	//! Get the current label brightness threshold
	double getDaylightLabelThreshold() const {return daylightLabelThreshold;}
	//! Return a brightness value based on objects in view (sky, sun, moon, ...)
	double getWorldAdaptationLuminance() const;

	//! Informing the drawer whether atmosphere is displayed.
	//! This is used to avoid twinkling/simulate extinction/refraction.
	//! Not required as property!
	void setFlagHasAtmosphere(bool b) {flagHasAtmosphere=b;}
	//! This is used to decide whether to apply refraction/extinction before rendering point sources et al.
	bool getFlagHasAtmosphere() const {return flagHasAtmosphere;}

	//! Set extinction coefficient, mag/airmass (for extinction).
	void setExtinctionCoefficient(double extCoeff);
	//! Get extinction coefficient, mag/airmass (for extinction).
	double getExtinctionCoefficient() const {return static_cast<double>(extinction.getExtinctionCoefficient());}
	//! Set atmospheric (ground) temperature in deg celsius (for refraction).
	void setAtmosphereTemperature(double celsius);
	//! Get atmospheric (ground) temperature in deg celsius (for refraction).
	double getAtmosphereTemperature() const {return static_cast<double>(refraction.getTemperature());}
	//! Set atmospheric (ground) pressure in mbar (for refraction).
	void setAtmospherePressure(double mbar);
	//! Get atmospheric (ground) pressure in mbar (for refraction).
	double getAtmospherePressure() const {return static_cast<double>(refraction.getPressure());}

	//! Get the current valid extinction computation object.
	const Extinction& getExtinction() const {return extinction;}
	//! Get the current valid refraction computation object.
	const Refraction& getRefraction() const {return refraction;}

	//! Get the radius of the big halo texture used when a 3d model is very bright.
	float getBig3dModelHaloRadius() const {return big3dModelHaloRadius;}
	//! Set the radius of the big halo texture used when a 3d model is very bright.
	void setBig3dModelHaloRadius(float r) {big3dModelHaloRadius=r;}

	// These are to find out the best sky parameters. Program feature for debugging/expert mode.
	// These will be connected from AtmosphereDialog and forward the settings to SkyLight class.

	void setFlagDrawSunAfterAtmosphere(bool val);
	bool getFlagDrawSunAfterAtmosphere() const {return flagDrawSunAfterAtmosphere;}
	void setFlagEarlySunHalo(bool val);
	bool getFlagEarlySunHalo() const {return flagEarlySunHalo;}

	bool getFlagTfromK(void) const {return flagTfromK;}
	void setFlagTfromK(bool val);

	double getT(void) const {return static_cast<double>(turbidity);}
	void setT(double newT);

signals:
	//! Emitted whenever the relative star scale changed
	void relativeStarScaleChanged(double b);
	//! Emitted whenever the absolute star scale changed
	void absoluteStarScaleChanged(double b);
	//! Emitted whenever the twinkle amount changed
	void twinkleAmountChanged(double b);
	//! Emitted whenever the twinkle flag is toggled
	void flagTwinkleChanged(bool b);
	//! Emitted whenever the force twinkle flag is toggled
	void flagForcedTwinkleChanged(bool b);
	//! Emitted whenever light pollution luminance changed
	void lightPollutionLuminanceChanged(double luminance);
	//! Emitted when flag to draw big halo around stars changed
	void flagDrawBigStarHaloChanged(bool b);
	//! Emitted on change of star texture
	void flagStarSpikyChanged(bool b);

	//! Emitted whenever the star magnitude limit flag is toggled
	void flagStarMagnitudeLimitChanged(bool b);
	//! Emitted whenever the nebula magnitude limit flag is toggled
	void flagNebulaMagnitudeLimitChanged(bool b);
	//! Emitted whenever the planet magnitude limit flag is toggled
	void flagPlanetMagnitudeLimitChanged(bool b);

	//! Emitted whenever the star magnitude limit changed
	void customStarMagLimitChanged(double limit);
	//! Emitted whenever the nebula magnitude limit changed
	void customNebulaMagLimitChanged(double limit);
	//! Emitted whenever the planet magnitude limit changed
	void customPlanetMagLimitChanged(double limit);

	//! Emitted whenever the luminance adaptation flag is toggled
	void flagLuminanceAdaptationChanged(bool b);
	//! Emitted when threshold value to draw info text in black is changed
	void daylightLabelThresholdChanged(double t);

	void extinctionCoefficientChanged(double coeff);
	void atmosphereTemperatureChanged(double celsius);
	void atmospherePressureChanged(double mbar);

	void flagTfromKChanged(bool b);
	void flagDrawSunAfterAtmosphereChanged(bool b);
	void flagEarlySunHaloChanged(bool b);
	void turbidityChanged(double t);

private:
	// Debug
	float reverseComputeRCMag(float rmag) const;

	//! Compute the current limit magnitude by dichotomy
	float computeLimitMagnitude() const;

	//! Compute the current limit luminance by dichotomy
	float computeLimitLuminance() const;

	//! Get StelSkyDrawer maximum FOV.
	float getMaxAdaptFov(void) const {return maxAdaptFov;}
	//! Set StelSkyDrawer maximum FOV.
	//! Usually stars/planet halos are drawn fainter when FOV gets larger,
	//! but when FOV gets larger than this value, the stars do not become
	//! fainter any more. Must be >= 60.0.
	void setMaxAdaptFov(float fov) {maxAdaptFov = (fov < 60.f) ? 60.f : fov;}

	//! Get StelSkyDrawer minimum FOV.
	float getMinAdaptFov(void) const {return minAdaptFov;}
	//! Set StelSkyDrawer minimum FOV.
	//! Usually stars/planet halos are drawn brighter when FOV gets smaller.
	//! But when FOV gets smaller than this value, the stars do not become
	//! brighter any more. Must be <= 60.0.
	void setMinAdaptFov(float fov) {minAdaptFov = (fov > 60.f) ? 60.f : fov;}

	//! Set the scaling applied to input luminance before they are converted by the StelToneReproducer
	void setInputScale(float in) {inScale = in;}
	//! Get the scaling applied to input luminance before they are converted by the StelToneReproducer
	float getInputScale() const {return inScale;}

	//! Compute the luminance for a point source with the given mag for the current FOV
	//! @param mag V magnitude of the point source
	//! @return the luminance in log(cd/m^2)
	inline float pointSourceMagToLuminance(float mag) const {return std::exp(pointSourceMagToLnLuminance(mag));}

	//! Compute the V magnitude for a point source with the given luminance for the current FOV
	//! @param lum the luminance in cd/m^2
	//! @return V magnitude of the point source
	float pointSourceLuminanceToMag(float lum) const;

	//! Compute the log of the luminance for a point source with the given mag for the current FOV
	//! @param mag V magnitude of the point source
	//! @return the luminance in cd/m^2
	float pointSourceMagToLnLuminance(float mag) const;

	//! Find the world adaptation luminance to use so that a point source of magnitude mag
	//! is displayed with a halo of size targetRadius
	float findWorldLumForMag(float mag, float targetRadius) const;

	//! Binds actual VAO if it's supported, sets up the relevant state manually otherwise.
	void bindVAO();
	//! Sets the vertex attribute states for the currently bound VAO so that glDraw* commands can work.
	void setupCurrentVAO();
	//! Binds zero VAO if VAO is supported, manually disables the relevant vertex attributes otherwise.
	void releaseVAO();

	StelCore* core;
	StelToneReproducer* eye;

	std::unique_ptr<QOpenGLVertexArrayObject> vao;
	std::unique_ptr<QOpenGLBuffer> vbo;

	Extinction extinction;
	Refraction refraction;

	float maxAdaptFov, minAdaptFov, lnfovFactor;
	bool flagStarTwinkle;      //! flickering stars due to atmospheric turbulence
	bool flagForcedTwinkle;    //! force flickering even if atmosphere is off
	double twinkleAmount;      //! magnitude range of atmospheric flicker at horizon
	bool flagDrawBigStarHalo;
	bool flagStarSpiky;

	//! Informing the drawer whether atmosphere is displayed.
	//! This is used to avoid twinkling/simulate extinction/refraction.
	bool flagHasAtmosphere;

	//! Controls the application of the user-defined star magnitude limit.
	//! @see customStarMagnitudeLimit
	bool flagStarMagnitudeLimit;
	//! Controls the application of the user-defined nebula magnitude limit.
	//! @see customNebulaMagnitudeLimit
	bool flagNebulaMagnitudeLimit;
	//! Controls the application of the user-defined planet magnitude limit.
	//! @see customPlanetMagnitudeLimit
	bool flagPlanetMagnitudeLimit;

	double starRelativeScale;
	double starAbsoluteScaleF;

	float starLinearScale;	// optimization variable

	//! Current magnitude limit for point sources
	float limitMagnitude;

	//! Current magnitude luminance
	float limitLuminance;

	//! User-defined magnitude limit for stars.
	//! Interpreted as a lower limit - stars fainter than this value will not be displayed.
	//! Used if flagStarMagnitudeLimit is true.
	double customStarMagLimit;
	//! User-defined magnitude limit for deep-sky objects.
	//! Interpreted as a lower limit - nebulae fainter than this value will not be displayed.
	//! Used if flagNebulaMagnitudeLimit is true.
	double customNebulaMagLimit;
	//! User-defined magnitude limit for solar system objects.
	//! Interpreted as a lower limit - planets fainter than this value will not be displayed.
	//! Used if flagPlanetMagnitudeLimit is true.
	double customPlanetMagLimit;

	//! Little halo texture
	QImage texImgHalo;
	QImage texImgHaloSpiky;
	StelTextureSP texHalo;
	StelTextureSP texHaloRayed;

	//! Load B-V conversion parameters from config file
	void initColorTableFromConfigFile(class QSettings* conf);

	//! Contains the list of colors matching a given B-V index
	static Vec3f colorTable[128];

	//! The current light pollution luminance
	double lightPollutionLuminance;

	//! The scaling applied to input luminance before they are converted by the StelToneReproducer
	float inScale;

	// Variables used for GL optimization when displaying point sources
	//! Vertex format for a point source.
	//! Texture pos is stored in another separately.
	struct StarVertex {
		Vec2f pos;
		unsigned char color[4];
	};
	static_assert(sizeof(StarVertex) == 12, "Size of StarVertex must be 12 bytes");
	
	//! Buffer for storing the vertex array data
	StarVertex* vertexArray;

	//! Buffer for storing the texture coordinate array data.
	unsigned char* textureCoordArray;
	
	class QOpenGLShaderProgram* starShaderProgram;
	struct StarShaderVars {
		int projectionMatrix;
		int texCoord;
		int pos;
		int color;
		int texture;
	};
	StarShaderVars starShaderVars;
	
	//! Current number of sources stored in the buffers (still to display)
	unsigned int nbPointSources;
	//! Maximum number of sources which can be stored in the buffers
	constexpr static unsigned int maxPointSources=1000;

	//! The maximum transformed luminance to apply at the next update
	float maxLum;
	//! The previously used world luminance
	float oldLum;

	//! Big halo texture
	StelTextureSP texBigHalo;
	StelTextureSP texSunHalo;
	StelTextureSP texSunCorona;

	//! Simulate the eye's luminance adaptation?
	bool flagLuminanceAdaptation;
	//! a customizable value in cd/m^2 which is used to decide when to render the InfoText in black (when sky is brighter than this)
	double daylightLabelThreshold;

	float big3dModelHaloRadius;
	bool flagDrawSunAfterAtmosphere; // Overdraw to avoid color mixing. Useful to allow deep-red suns, but may also look ugly.
	bool flagEarlySunHalo; // Used to select if solar halo is plotted before (true) or after (default, false) the 3D sphere.
	bool flagTfromK; // true to compute T from extinction k
	float turbidity; // atmospheric turbidity: Preetham sky usable for 2<T<6, best at T=5.
};

#endif // STELSKYDRAWER_HPP
