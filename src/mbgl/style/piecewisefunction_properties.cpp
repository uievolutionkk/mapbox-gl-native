#include <mbgl/style/piecewisefunction_properties.hpp>
#include <mbgl/style/types.hpp>

#include <cmath>

namespace mbgl {

template <typename T>
size_t getBiggestStopLessThan(std::vector<std::pair<float, T>> stops, float z) {
    for (uint32_t i = 0; i < stops.size(); i++) {
        if (stops[i].first > z) {
            return i == 0 ? i : i - 1;
        }
    }
    return stops.size() - 1;
}

template <typename T>
T PiecewiseConstantFunction<T>::evaluate(const StyleCalculationParameters& parameters) const {
    T result;

    float z = parameters.z;
    float fraction = std::fmod(z, 1.0f);
    std::chrono::duration<float> d = duration ? *duration : parameters.defaultFadeDuration;
    float t = std::min((Clock::now() - parameters.zoomHistory.lastIntegerZoomTime) / d, 1.0f);
    float fromScale = 1.0f;
    float toScale = 1.0f;
    size_t from, to;

    if (z > parameters.zoomHistory.lastIntegerZoom) {
        result.t = fraction + (1.0f - fraction) * t;
        from = getBiggestStopLessThan(values, z - 1.0f);
        to = getBiggestStopLessThan(values, z);
        fromScale *= 2.0f;

    } else {
        result.t = 1 - (1 - t) * fraction;
        to = getBiggestStopLessThan(values, z);
        from = getBiggestStopLessThan(values, z + 1.0f);
        fromScale /= 2.0f;
    }


    result.from = values[from].second.to;
    result.to = values[to].second.to;
    result.fromScale = fromScale;
    result.toScale = toScale;
    return result;
}

template Faded<std::string> PiecewiseConstantFunction<Faded<std::string>>::evaluate(const StyleCalculationParameters&) const;
template Faded<std::vector<float>> PiecewiseConstantFunction<Faded<std::vector<float>>>::evaluate(const StyleCalculationParameters&) const;

}
