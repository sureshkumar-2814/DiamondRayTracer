#pragma once

#include <cmath>
#include <glm/glm.hpp>

// ============= SPECTRAL CONSTANTS =============
constexpr float MIN_WAVELENGTH = 380.0f;  // UV edge (nm)
constexpr float MAX_WAVELENGTH = 780.0f;  // IR edge (nm)

// Standard visible wavelengths (nm) - Used for sampling
constexpr float VISIBLE_WAVELENGTHS[] = {
    380.0f, 420.0f, 460.0f, 500.0f, 540.0f, 580.0f, 
    620.0f, 660.0f, 700.0f, 740.0f, 780.0f
};
constexpr int NUM_VISIBLE_WAVELENGTHS = 11;

// ============= SELLMEIER EQUATION FOR DIAMOND =============
// More accurate than Cauchy across the visible spectrum
// n² = 1 + B1*λ²/(λ²-C1) + B2*λ²/(λ²-C2)
inline float getDiamondIOR_Sellmeier(float wavelength_nm) {
    float lambda_um = wavelength_nm / 1000.0f;
    float l2 = lambda_um * lambda_um;
    
    // Sellmeier coefficients for diamond (CRC Handbook)
    constexpr float B1 = 0.3306f;
    constexpr float C1 = 0.1750f * 0.1750f;
    constexpr float B2 = 4.3356f;
    constexpr float C2 = 0.1060f * 0.1060f;
    
    float n2 = 1.0f + B1 * l2 / (l2 - C1) + B2 * l2 / (l2 - C2);
    return std::sqrt(std::max(n2, 1.0f));
}

// ============= WAVELENGTH TO RGB CONVERSION =============
// CIE 1931 standard observer wavelength to RGB
// Reference: https://en.wikipedia.org/wiki/Wavelength#Visible_spectrum
inline glm::vec3 wavelengthToRGB(float wavelength_nm) {
    float wave = wavelength_nm;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    
    // UV/Violet (380-420nm)
    if (wave < 420.0f) {
        r = -(wave - 380.0f) / (420.0f - 380.0f);
        r = 0.3f + 0.7f * r;
        b = 1.0f;
    }
    // Violet-Blue (420-490nm)
    else if (wave < 490.0f) {
        b = 1.0f;
        g = (wave - 420.0f) / (490.0f - 420.0f);
    }
    // Blue-Cyan (490-575nm)
    else if (wave < 575.0f) {
        b = 1.0f - (wave - 490.0f) / (575.0f - 490.0f);
        g = 1.0f;
    }
    // Cyan-Green (575-585nm)
    else if (wave < 585.0f) {
        r = 0.0f;
        g = 1.0f;
        b = 0.0f;
    }
    // Green-Yellow (585-620nm)
    else if (wave < 620.0f) {
        r = (wave - 585.0f) / (620.0f - 585.0f);
        g = 1.0f;
    }
    // Yellow-Red (620-750nm)
    else if (wave < 750.0f) {
        r = 1.0f;
        g = 1.0f - (wave - 620.0f) / (750.0f - 620.0f);
    }
    // Red (750-780nm)
    else {
        r = 1.0f + 0.3f * ((780.0f - wave) / (780.0f - 750.0f));
        r = std::min(r, 1.0f);
    }
    
    // Intensity correction (dimmer at spectrum edges)
    float intensity = 1.0f;
    if (wave < 420.0f || wave > 700.0f) {
        intensity = 0.3f + 0.7f * (1.0f - std::abs(wave - 550.0f) / 170.0f);
    }
    
    return glm::vec3(r, g, b) * intensity;
}

// ============= UTILITY FUNCTIONS =============

// Get diamond IOR for a specific wavelength (Sellmeier approximation)
inline float getDiamondIOR(float wavelength_nm) {
    return getDiamondIOR_Sellmeier(wavelength_nm);
}

// Linear interpolation between two values
inline float mix(float a, float b, float t) {
    return a * (1.0f - t) + b * t;
}

// Clamp value between min and max
inline float clamp(float value, float min_val, float max_val) {
    return std::max(min_val, std::min(value, max_val));
}

// ============= DISPERSION CALCULATION =============
// Returns the difference in IOR between red and blue light
inline float getDispersionValue(float red_wavelength_nm, float blue_wavelength_nm) {
    float ior_red = getDiamondIOR(red_wavelength_nm);
    float ior_blue = getDiamondIOR(blue_wavelength_nm);
    return ior_blue - ior_red;
}

// Calculate dispersion for standard red (656.3nm) and blue (486.1nm)
inline float getDiamondDispersion() {
    return getDispersionValue(656.3f, 486.1f);  // Should be ~0.044
}
