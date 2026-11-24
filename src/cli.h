#pragma once

#include <QStringList>
#include <optional>

namespace Cli {

// Returns true when the command-line invocation runs headlessly without a GUI,
// so main() can set QT_QPA_PLATFORM=offscreen before creating QGuiApplication.
bool isHeadlessCommand(int argc, char *argv[]);

// Runs a CLI subcommand (`printers`, `export`, `print`, `skill`, `--help`,
// `--version`) and returns its exit code, or std::nullopt when the GUI should
// launch with `guiFiles` populated.
std::optional<int> run(const QStringList &arguments, QStringList &guiFiles);

} // namespace Cli
