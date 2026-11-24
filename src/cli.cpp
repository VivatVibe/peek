#include "cli.h"
#include "backend.h"
#include "imageops.h"
#include "presetstore.h"
#include "printrenderer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

namespace {

void printJson(const QJsonObject &obj, bool toStderr = false) {
    QTextStream out(toStderr ? stderr : stdout);
    out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << '\n';
}

void printError(const QString &message, bool json) {
    if (json) {
        printJson({{"ok", false}, {"error", message}}, true);
    } else {
        QTextStream err(stderr);
        err << "peek: " << message << '\n';
    }
}

void printUsage() {
    QTextStream out(stdout);
    out << "Usage:\n"
        << "  peek [open] [image ...]              Open images in the viewer\n"
        << "  peek printers [--json]               List CUPS printers\n"
        << "  peek presets [--json]                List print presets\n"
        << "  peek export <image ...> -o <out> [options] [--json]\n"
        << "                                       Export image (PNG/JPG/WebP/TIFF/BMP) or PDF\n"
        << "  peek print <image ...> [--printer NAME] [--preset NAME] [--copies N] [options] [--json]\n"
        << "                                       Print image(s) via CUPS\n"
        << "  peek skill install                   Install the Peek agent skill\n"
        << "\n"
        << "Edit & export options:\n"
        << "  --format png|jpg|webp|tiff|bmp|pdf   Output format (defaults from -o extension)\n"
        << "  --quality <10-100>                   JPEG/WebP quality (default: 90)\n"
        << "  --crop <x,y,w,h>                     Crop rectangle in pixels\n"
        << "  --resize <WxH>                       Resize target in pixels\n"
        << "  --rotate <0|90|180|270>              Rotate clockwise\n"
        << "  --exposure <-1.0..1.0>               Exposure adjustment\n"
        << "  --contrast <-1.0..1.0>               Contrast adjustment\n"
        << "  --saturation <-1.0..1.0>             Saturation adjustment\n"
        << "  --brightness <-1.0..1.0>             Brightness adjustment\n"
        << "  --auto-levels                        Auto-balance brightness and contrast\n"
        << "\n"
        << "Print layout options:\n"
        << "  --preset <name-or-id>                Load a saved or built-in print preset\n"
        << "  --paper a4|letter|4x6|5x7|a5|a3|legal\n"
        << "  --orientation auto|portrait|landscape\n"
        << "  --fit fit|fill|scale\n"
        << "  --scale <10-200>\n"
        << "  --margins standard|narrow|borderless\n"
        << "  --per-page 1|2|4|6|9                 Images per sheet\n"
        << "  --cut-guides                         Draw dashed trim guides\n"
        << "  --grayscale\n";
}

bool queryPrintersSync(QStringList &printers, QString &defaultPrinter, QString &error) {
    const QString lpstat = QStandardPaths::findExecutable("lpstat");
    if (lpstat.isEmpty()) {
        error = "CUPS (lpstat) is not installed.";
        return false;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    proc.setProcessEnvironment(env);
    proc.start(lpstat, {"-p", "-d"});
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        error = "Timed out querying CUPS printers.";
        return false;
    }

    Backend::parseLpstatOutput(QString::fromUtf8(proc.readAllStandardOutput()), printers, defaultPrinter);
    if (printers.isEmpty() && proc.exitCode() != 0) {
        const QString errText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        error = errText.isEmpty() ? "CUPS is not responding." : errText;
        return false;
    }
    return true;
}

struct CliJobOptions {
    PrintSettings print;
    EditState edit;
    bool autoLevels = false;
    QString format;
    int quality = 90;
    QString outputPath;
    QString printerName;
    bool json = false;
    QStringList positionals;
};

bool parseDoubleRange(const QString &text, double lo, double hi, double &out, const QString &flag, QString &error) {
    bool ok = false;
    const double val = text.toDouble(&ok);
    if (!ok || val < lo || val > hi) {
        error = QString("%1 must be between %2 and %3.").arg(flag).arg(lo).arg(hi);
        return false;
    }
    out = val;
    return true;
}

bool parseJobOptions(const QStringList &args, CliJobOptions &opts, QString &error) {
    // Pre-scan for --preset so explicit CLI flags always override preset values.
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == "--preset") {
            PresetStore store;
            PrintPreset found;
            if (!store.findByNameOrId(args.at(i + 1), found)) {
                error = "Unknown preset: " + args.at(i + 1);
                return false;
            }
            opts.print = found.settings;
            if (!found.printer.isEmpty())
                opts.printerName = found.printer;
            break;
        }
    }

    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        auto requireValue = [&](QString &dest) -> bool {
            if (i + 1 >= args.size()) {
                error = "Missing value for " + arg;
                return false;
            }
            dest = args.at(++i);
            return true;
        };

        if (arg == "--json") {
            opts.json = true;
        } else if (arg == "--preset") {
            QString ignored;
            if (!requireValue(ignored)) return false;
        } else if (arg == "--grayscale") {
            opts.print.grayscale = true;
        } else if (arg == "--cut-guides") {
            opts.print.cutGuides = true;
        } else if (arg == "--auto-levels") {
            opts.autoLevels = true;
        } else if (arg == "-o" || arg == "--output") {
            if (!requireValue(opts.outputPath)) return false;
        } else if (arg == "--printer" || arg == "-d") {
            if (!requireValue(opts.printerName)) return false;
        } else if (arg == "--format") {
            QString v;
            if (!requireValue(v)) return false;
            opts.format = ImageOps::normalizedFormat(v);
        } else if (arg == "--quality") {
            QString v;
            if (!requireValue(v)) return false;
            bool ok = false;
            const int q = v.toInt(&ok);
            if (!ok || q < 10 || q > 100) {
                error = "Quality must be between 10 and 100.";
                return false;
            }
            opts.quality = q;
        } else if (arg == "--crop") {
            QString v;
            if (!requireValue(v)) return false;
            const QStringList parts = v.split(',');
            if (parts.size() != 4) {
                error = "Crop must be x,y,w,h.";
                return false;
            }
            bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
            const QRect rect(parts[0].toInt(&ok1), parts[1].toInt(&ok2), parts[2].toInt(&ok3), parts[3].toInt(&ok4));
            if (!ok1 || !ok2 || !ok3 || !ok4 || rect.width() <= 0 || rect.height() <= 0) {
                error = "Invalid crop rectangle.";
                return false;
            }
            opts.edit.cropRect = rect;
        } else if (arg == "--resize") {
            QString v;
            if (!requireValue(v)) return false;
            const QStringList parts = v.toLower().split('x');
            if (parts.size() != 2) {
                error = "Resize must be WxH (for example 1280x720).";
                return false;
            }
            bool ok1 = false, ok2 = false;
            const QSize sz(parts[0].toInt(&ok1), parts[1].toInt(&ok2));
            if (!ok1 || !ok2 || sz.width() <= 0 || sz.height() <= 0) {
                error = "Invalid resize dimensions.";
                return false;
            }
            opts.edit.resizeTarget = sz;
        } else if (arg == "--exposure") {
            QString v;
            if (!requireValue(v) || !parseDoubleRange(v, -1.0, 1.0, opts.edit.color.exposure, arg, error)) return false;
        } else if (arg == "--contrast") {
            QString v;
            if (!requireValue(v) || !parseDoubleRange(v, -1.0, 1.0, opts.edit.color.contrast, arg, error)) return false;
        } else if (arg == "--saturation") {
            QString v;
            if (!requireValue(v) || !parseDoubleRange(v, -1.0, 1.0, opts.edit.color.saturation, arg, error)) return false;
        } else if (arg == "--brightness") {
            QString v;
            if (!requireValue(v) || !parseDoubleRange(v, -1.0, 1.0, opts.edit.color.brightness, arg, error)) return false;
        } else if (arg == "--paper") {
            QString v;
            if (!requireValue(v)) return false;
            opts.print.paperSize = PrintRenderer::paperSpec(v).id;
        } else if (arg == "--orientation") {
            QString v;
            if (!requireValue(v)) return false;
            opts.print.orientation = (v == "portrait" || v == "landscape") ? v : "auto";
        } else if (arg == "--fit") {
            QString v;
            if (!requireValue(v)) return false;
            opts.print.scaleMode = (v == "fill" || v == "scale") ? v : "fit";
        } else if (arg == "--scale") {
            QString v;
            if (!requireValue(v)) return false;
            bool ok = false;
            const int pct = v.toInt(&ok);
            if (!ok || pct < 10 || pct > 200) {
                error = "Scale must be between 10 and 200.";
                return false;
            }
            opts.print.scaleMode = "scale";
            opts.print.scalePercent = pct;
        } else if (arg == "--margins") {
            QString v;
            if (!requireValue(v)) return false;
            opts.print.marginMode = (v == "narrow" || v == "borderless") ? v : "standard";
        } else if (arg == "--per-page") {
            QString v;
            if (!requireValue(v)) return false;
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || (n != 1 && n != 2 && n != 4 && n != 6 && n != 9)) {
                error = "Per-page must be 1, 2, 4, 6, or 9.";
                return false;
            }
            opts.print.perPage = n;
        } else if (arg == "--copies" || arg == "-n") {
            QString v;
            if (!requireValue(v)) return false;
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n < 1 || n > 99) {
                error = "Copies must be between 1 and 99.";
                return false;
            }
            opts.print.copies = n;
        } else if (arg == "--rotate") {
            QString v;
            if (!requireValue(v)) return false;
            bool ok = false;
            const int deg = v.toInt(&ok);
            if (!ok || (deg % 90) != 0) {
                error = "Rotation must be a multiple of 90 degrees.";
                return false;
            }
            opts.edit.rotation = ((deg % 360) + 360) % 360;
        } else if (arg.startsWith('-')) {
            error = "Unknown option: " + arg;
            return false;
        } else {
            opts.positionals.append(arg);
        }
    }
    return true;
}

int installSkill(bool json) {
    QFile bundled(":/skill/SKILL.md");
    if (!bundled.open(QIODevice::ReadOnly)) {
        printError("Bundled skill file is missing.", json);
        return 1;
    }
    const QByteArray data = bundled.readAll();

    const QString destDir = QDir::homePath() + "/.agents/skills/peek";
    if (!QDir().mkpath(destDir)) {
        printError("Could not create " + destDir, json);
        return 1;
    }

    const QString destPath = destDir + "/SKILL.md";
    const QString tmpPath = destDir + "/.SKILL.md.tmp";
    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
        QFile::remove(tmpPath);
        printError("Could not write " + destPath, json);
        return 1;
    }
    out.close();
    QFile::remove(destPath);
    if (!QFile::rename(tmpPath, destPath)) {
        QFile::remove(tmpPath);
        printError("Could not install " + destPath, json);
        return 1;
    }

    if (json) {
        printJson({{"ok", true}, {"path", destPath}});
    } else {
        QTextStream stream(stdout);
        stream << "Installed Peek skill to " << destPath << '\n';
    }
    return 0;
}

} // namespace

namespace Cli {

bool isHeadlessCommand(int argc, char *argv[]) {
    if (argc < 2)
        return false;
    const QString cmd = QString::fromLocal8Bit(argv[1]);
    return cmd == "printers" || cmd == "presets" || cmd == "export" || cmd == "print" || cmd == "skill" ||
           cmd == "--help" || cmd == "-h" || cmd == "--version" || cmd == "-v";
}

std::optional<int> run(const QStringList &arguments, QStringList &guiFiles) {
    guiFiles.clear();
    if (arguments.size() <= 1)
        return std::nullopt;

    const QString cmd = arguments.at(1);
    const QStringList rest = arguments.mid(2);

    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }
    if (cmd == "--version" || cmd == "-v") {
        QTextStream out(stdout);
        out << "peek 0.4.0\n";
        return 0;
    }
    if (cmd == "open") {
        guiFiles = rest;
        return std::nullopt;
    }
    if (cmd == "skill") {
        const bool json = rest.contains("--json");
        if (!rest.isEmpty() && rest.first() == "install")
            return installSkill(json);
        printError("Usage: peek skill install", json);
        return 1;
    }
    if (cmd == "presets") {
        const bool json = rest.contains("--json");
        PresetStore store;
        const auto all = store.allPresets();
        if (json) {
            QJsonArray arr;
            for (const auto &p : all) {
                arr.append(QJsonObject{
                    {"id", p.id},
                    {"name", p.name},
                    {"builtIn", p.builtIn},
                    {"settings", PresetStore::settingsToJson(p.settings, p.printer)}
                });
            }
            printJson({{"ok", true}, {"active", store.activePresetId()}, {"presets", arr}});
        } else {
            QTextStream out(stdout);
            for (const auto &p : all)
                out << p.name << (p.id == store.activePresetId() ? " (active)" : "") << '\n';
        }
        return 0;
    }
    if (cmd == "printers") {
        const bool json = rest.contains("--json");
        QStringList printers;
        QString defPrinter;
        QString error;
        if (!queryPrintersSync(printers, defPrinter, error)) {
            printError(error, json);
            return 1;
        }
        if (json) {
            QJsonArray arr;
            for (const QString &p : printers)
                arr.append(p);
            printJson({{"ok", true}, {"default", defPrinter}, {"printers", arr}});
        } else {
            QTextStream out(stdout);
            for (const QString &p : printers)
                out << p << (p == defPrinter ? " (default)" : "") << '\n';
        }
        return 0;
    }
    if (cmd == "export" || cmd == "print") {
        CliJobOptions opts;
        QString parseError;
        if (!parseJobOptions(rest, opts, parseError)) {
            printError(parseError, opts.json);
            return 1;
        }
        if (opts.positionals.isEmpty()) {
            printError("Specify at least one image file.", opts.json);
            return 1;
        }

        QVector<QImage> images;
        for (const QString &imagePath : opts.positionals) {
            QImageReader reader(imagePath);
            reader.setAutoTransform(true);
            const QImage loaded = reader.read();
            if (loaded.isNull()) {
                printError("Could not read image " + imagePath + ": " + reader.errorString(), opts.json);
                return 1;
            }
            EditState state = opts.edit;
            if (opts.autoLevels) {
                EditState geomOnly = state;
                geomOnly.color = {};
                state.color = ImageOps::computeAutoLevels(ImageOps::applyEdits(loaded, geomOnly, false));
            }
            images.append(ImageOps::applyEdits(loaded, state, opts.print.grayscale));
        }

        if (cmd == "export") {
            if (opts.outputPath.isEmpty()) {
                printError("Specify an output path with -o <file>.", opts.json);
                return 1;
            }
            const QString inferredExt = QFileInfo(opts.outputPath).suffix().toLower();
            const QString format = !opts.format.isEmpty()
                                       ? opts.format
                                       : (inferredExt.isEmpty() ? "pdf" : ImageOps::normalizedFormat(inferredExt));

            if (format != "pdf") {
                QString exportError;
                if (!ImageOps::exportImageAtomic(images.first(), opts.outputPath, format, opts.quality, &exportError)) {
                    printError(exportError, opts.json);
                    return 1;
                }
                if (opts.json) {
                    printJson({{"ok", true},
                               {"output", QFileInfo(opts.outputPath).absoluteFilePath()},
                               {"format", format},
                               {"width", images.first().width()},
                               {"height", images.first().height()}});
                } else {
                    QTextStream out(stdout);
                    out << "Exported " << QFileInfo(opts.outputPath).absoluteFilePath() << '\n';
                }
                return 0;
            }

            const bool repeatSingle = images.size() == 1;
            const QVector<QVector<QImage>> pages = PrintRenderer::paginateImages(images, opts.print.perPage, repeatSingle);
            QString exportError;
            if (!PrintRenderer::exportPdf(opts.outputPath, pages, opts.print, &exportError)) {
                printError(exportError, opts.json);
                return 1;
            }
            if (opts.json) {
                printJson({{"ok", true},
                           {"output", QFileInfo(opts.outputPath).absoluteFilePath()},
                           {"format", "pdf"},
                           {"pages", static_cast<int>(pages.size())}});
            } else {
                QTextStream out(stdout);
                out << "Exported " << QFileInfo(opts.outputPath).absoluteFilePath() << '\n';
            }
            return 0;
        }

        // Print subcommand.
        const QString lp = QStandardPaths::findExecutable("lp");
        if (lp.isEmpty()) {
            printError("CUPS (lp) is not installed.", opts.json);
            return 1;
        }
        if (opts.printerName.isEmpty()) {
            QStringList printers;
            QString defPrinter;
            QString error;
            if (!queryPrintersSync(printers, defPrinter, error) || printers.isEmpty()) {
                printError(error.isEmpty() ? "No printers configured." : error, opts.json);
                return 1;
            }
            opts.printerName = !defPrinter.isEmpty() ? defPrinter : printers.first();
        }

        const bool repeatSingle = images.size() == 1;
        const QVector<QVector<QImage>> pages = PrintRenderer::paginateImages(images, opts.print.perPage, repeatSingle);
        const QString tempPdf = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                    .filePath("peek-cli-" + QUuid::createUuid().toString(QUuid::Id128) + ".pdf");
        QString exportError;
        if (!PrintRenderer::exportPdf(tempPdf, pages, opts.print, &exportError)) {
            printError(exportError, opts.json);
            return 1;
        }

        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LC_ALL", "C");
        proc.setProcessEnvironment(env);
        proc.start(lp, Backend::buildLpArguments(opts.printerName, opts.print, tempPdf, QFileInfo(opts.positionals.first()).fileName()));
        const bool finished = proc.waitForFinished(15000);
        QFile::remove(tempPdf);

        if (!finished || proc.exitCode() != 0) {
            const QString errText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            printError(errText.isEmpty() ? "Failed to submit print job." : errText, opts.json);
            return 1;
        }

        const QString stdoutText = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        static const QRegularExpression jobRe("request id is (\\S+)");
        const auto match = jobRe.match(stdoutText);
        const QString jobId = match.hasMatch() ? match.captured(1) : QString();

        if (opts.json) {
            printJson({{"ok", true},
                       {"printer", opts.printerName},
                       {"job", jobId},
                       {"pages", static_cast<int>(pages.size())},
                       {"copies", opts.print.copies}});
        } else {
            QTextStream out(stdout);
            out << "Sent to " << opts.printerName << (jobId.isEmpty() ? "" : " (" + jobId + ")") << '\n';
        }
        return 0;
    }

    guiFiles = arguments.mid(1);
    return std::nullopt;
}

} // namespace Cli
