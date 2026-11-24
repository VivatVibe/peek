#include "cli.h"
#include "backend.h"
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
        << "  peek export <image> -o <out.pdf> [options] [--json]\n"
        << "                                       Export a print-ready PDF\n"
        << "  peek print <image> [--printer NAME] [--copies N] [options] [--json]\n"
        << "                                       Print an image via CUPS\n"
        << "  peek skill install                   Install the Peek agent skill\n"
        << "\n"
        << "Layout options:\n"
        << "  --paper a4|letter|4x6|5x7|a5|a3|legal\n"
        << "  --orientation auto|portrait|landscape\n"
        << "  --fit fit|fill|scale\n"
        << "  --scale <10-200>\n"
        << "  --margins standard|narrow|borderless\n"
        << "  --grayscale\n"
        << "  --rotate <0|90|180|270>\n";
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

bool parseLayoutOptions(const QStringList &args,
                        PrintSettings &settings,
                        int &rotation,
                        QString &outputPath,
                        QString &printerName,
                        bool &json,
                        QStringList &positionals,
                        QString &error) {
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
            json = true;
        } else if (arg == "--grayscale") {
            settings.grayscale = true;
        } else if (arg == "-o" || arg == "--output") {
            if (!requireValue(outputPath))
                return false;
        } else if (arg == "--printer" || arg == "-d") {
            if (!requireValue(printerName))
                return false;
        } else if (arg == "--paper") {
            QString v;
            if (!requireValue(v))
                return false;
            settings.paperSize = PrintRenderer::paperSpec(v).id;
        } else if (arg == "--orientation") {
            QString v;
            if (!requireValue(v))
                return false;
            settings.orientation = (v == "portrait" || v == "landscape") ? v : "auto";
        } else if (arg == "--fit") {
            QString v;
            if (!requireValue(v))
                return false;
            settings.scaleMode = (v == "fill" || v == "scale") ? v : "fit";
        } else if (arg == "--scale") {
            QString v;
            if (!requireValue(v))
                return false;
            bool ok = false;
            const int pct = v.toInt(&ok);
            if (!ok || pct < 10 || pct > 200) {
                error = "Scale must be between 10 and 200.";
                return false;
            }
            settings.scaleMode = "scale";
            settings.scalePercent = pct;
        } else if (arg == "--margins") {
            QString v;
            if (!requireValue(v))
                return false;
            settings.marginMode = (v == "narrow" || v == "borderless") ? v : "standard";
        } else if (arg == "--copies" || arg == "-n") {
            QString v;
            if (!requireValue(v))
                return false;
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n < 1 || n > 99) {
                error = "Copies must be between 1 and 99.";
                return false;
            }
            settings.copies = n;
        } else if (arg == "--rotate") {
            QString v;
            if (!requireValue(v))
                return false;
            bool ok = false;
            const int deg = v.toInt(&ok);
            if (!ok || (deg % 90) != 0) {
                error = "Rotation must be a multiple of 90 degrees.";
                return false;
            }
            rotation = ((deg % 360) + 360) % 360;
        } else if (arg.startsWith('-')) {
            error = "Unknown option: " + arg;
            return false;
        } else {
            positionals.append(arg);
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
    return cmd == "printers" || cmd == "export" || cmd == "print" || cmd == "skill" ||
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
        out << "peek 0.1.0\n";
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
        PrintSettings settings;
        int rotation = 0;
        QString outputPath;
        QString printerName;
        bool json = false;
        QStringList positionals;
        QString parseError;
        if (!parseLayoutOptions(rest, settings, rotation, outputPath, printerName, json, positionals, parseError)) {
            printError(parseError, json);
            return 1;
        }
        if (positionals.isEmpty()) {
            printError("Specify an image file.", json);
            return 1;
        }

        const QString imagePath = positionals.first();
        QImageReader reader(imagePath);
        reader.setAutoTransform(true);
        const QImage loaded = reader.read();
        if (loaded.isNull()) {
            printError("Could not read image: " + reader.errorString(), json);
            return 1;
        }

        const QImage transformed = PrintRenderer::applyTransforms(loaded, rotation, false, false, settings.grayscale);

        if (cmd == "export") {
            if (outputPath.isEmpty()) {
                printError("Specify an output PDF path with -o <file.pdf>.", json);
                return 1;
            }
            QString exportError;
            if (!PrintRenderer::exportPdf(outputPath, transformed, settings, &exportError)) {
                printError(exportError, json);
                return 1;
            }
            if (json) {
                printJson({{"ok", true}, {"output", QFileInfo(outputPath).absoluteFilePath()}});
            } else {
                QTextStream out(stdout);
                out << "Exported " << QFileInfo(outputPath).absoluteFilePath() << '\n';
            }
            return 0;
        }

        // Print subcommand
        const QString lp = QStandardPaths::findExecutable("lp");
        if (lp.isEmpty()) {
            printError("CUPS (lp) is not installed.", json);
            return 1;
        }
        if (printerName.isEmpty()) {
            QStringList printers;
            QString defPrinter;
            QString error;
            if (!queryPrintersSync(printers, defPrinter, error) || printers.isEmpty()) {
                printError(error.isEmpty() ? "No printers configured." : error, json);
                return 1;
            }
            printerName = !defPrinter.isEmpty() ? defPrinter : printers.first();
        }

        const QString tempPdf = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                    .filePath("peek-cli-" + QUuid::createUuid().toString(QUuid::Id128) + ".pdf");
        QString exportError;
        if (!PrintRenderer::exportPdf(tempPdf, transformed, settings, &exportError)) {
            printError(exportError, json);
            return 1;
        }

        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LC_ALL", "C");
        proc.setProcessEnvironment(env);
        proc.start(lp, Backend::buildLpArguments(printerName, settings, tempPdf, QFileInfo(imagePath).fileName()));
        const bool finished = proc.waitForFinished(15000);
        QFile::remove(tempPdf);

        if (!finished || proc.exitCode() != 0) {
            const QString errText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            printError(errText.isEmpty() ? "Failed to submit print job." : errText, json);
            return 1;
        }

        const QString stdoutText = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        static const QRegularExpression jobRe("request id is (\\S+)");
        const auto match = jobRe.match(stdoutText);
        const QString jobId = match.hasMatch() ? match.captured(1) : QString();

        if (json) {
            printJson({{"ok", true}, {"printer", printerName}, {"job", jobId}, {"copies", settings.copies}});
        } else {
            QTextStream out(stdout);
            out << "Sent to " << printerName << (jobId.isEmpty() ? "" : " (" + jobId + ")") << '\n';
        }
        return 0;
    }

    // Otherwise treat all arguments as image paths to open in the GUI.
    guiFiles = arguments.mid(1);
    return std::nullopt;
}

} // namespace Cli
