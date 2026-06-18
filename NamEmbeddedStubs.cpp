// Minimal embedded implementations of NAM host-library functions.
// These replace get_dsp.cpp for ARM targets where <filesystem>/<mutex>/<regex>
// are unavailable. Only the binary NAMB load path is supported.

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "NeuralAmpModelerCore/NAM/get_dsp.h"
#include "NeuralAmpModelerCore/NAM/model_config.h"

namespace nam
{

Version ParseVersion(const std::string& versionStr)
{
  int maj = 0, min = 0, pat = 0;
  sscanf(versionStr.c_str(), "%d.%d.%d", &maj, &min, &pat);
  return Version(maj, min, pat);
}

void register_version_support_checker(std::shared_ptr<const IVersionSupportChecker>)
{
  // Not supported on embedded — binary NAMB loader performs its own validation.
}

Supported is_version_supported(const std::string)
{
  return Supported::YES;
}

void verify_config_version(const std::string)
{
  // Binary NAMB loader handles version checking internally.
}

namespace
{
void apply_metadata(DSP& dsp, const ModelMetadata& metadata)
{
  if (metadata.loudness.has_value())
    dsp.SetLoudness(metadata.loudness.value());
  if (metadata.input_level.has_value())
    dsp.SetInputLevel(metadata.input_level.value());
  if (metadata.output_level.has_value())
    dsp.SetOutputLevel(metadata.output_level.value());
}
} // anonymous namespace

std::unique_ptr<DSP> create_dsp(std::unique_ptr<ModelConfig> config, std::vector<float> weights,
                                const ModelMetadata& metadata)
{
  auto out = config->create(std::move(weights), metadata.sample_rate);
  apply_metadata(*out, metadata);
  return out;
}

std::unique_ptr<DSP> get_dsp(dspData& conf, DspLoadOptions)
{
  throw std::runtime_error("JSON model loading not supported on embedded targets.");
}

} // namespace nam
