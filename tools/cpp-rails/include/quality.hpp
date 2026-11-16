#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cpprails {
struct QualityEvidence {
    std::string path;
    std::size_t line = 0;
    std::string message;
};
struct QualityDimension {
    std::string name;
    std::string status;
    // When present: factual signals / production KLOC, never a quality grade.
    std::optional<double> score;
    std::size_t signals = 0;
    std::vector<QualityEvidence> evidence;
};
struct QualityReport {
    std::size_t code_lines = 0, production_lines = 0, test_lines = 0, files = 0;
    std::vector<QualityDimension> dimensions;
};
// Only the supplied files are measured. Relative paths resolve against root.
// Unreadable files throw; caller must not publish a partial report as complete.
QualityReport quality_score(const std::filesystem::path& root,
                            const std::vector<std::filesystem::path>& files);
std::string quality_json(const QualityReport& report);
std::string quality_markdown(const QualityReport& report);
} // namespace cpprails
