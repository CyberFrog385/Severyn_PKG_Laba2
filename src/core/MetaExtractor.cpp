#include "core/MetaExtractor.h"

#include <vector>

#include "core/ByteReader.h"
#include "core/FormatDetector.h"
#include "core/parsers/ParserCommon.h"
#include "core/parsers/Parsers.h"

namespace lab2 {

namespace {

constexpr std::size_t kProbeBytes = 64;

void addCommonFields(ImageRecord& record, const Detection& detection) {
    addField(record, "Формат", "Определён по сигнатуре", formatShortName(detection.format),
             "Формат определяется по содержимому файла, а не по расширению, поэтому "
             "переименованный файл распознаётся правильно.");
    if (!detection.signature.empty()) {
        addField(record, "Формат", "Байты сигнатуры", detection.signature,
                 "Первые байты файла в шестнадцатеричном виде.");
    }
    addField(record, "Формат", "Расширение файла",
             record.extensionName.empty() ? std::string("нет расширения") : record.extensionName,
             "Приведено для сравнения с сигнатурой: несоответствие не делает файл битым.");
    if (record.extensionMismatch) {
        addField(record, "Формат", "Несоответствие расширения",
                 "расширение " + record.extensionName + ", сигнатура " +
                     formatShortName(record.format),
                 "Файл переименован или имеет неверное расширение; данные разобраны по сигнатуре.");
    }
    addField(record, "Формат", "Полное название", formatFullName(record.format),
             "Название формата по его спецификации.");
}

void addProblemFields(ImageRecord& record) {
    if (record.problems.empty()) {
        addField(record, "Проверка", "Замечания", "нет",
                 "Структура файла согласована: размеры, смещения и завершающие маркеры в норме.");
        return;
    }
    int index = 0;
    for (const std::string& problem : record.problems) {
        addField(record, "Проверка", "Замечание " + text(++index), problem,
                 "Выводится, когда данные файла противоречат его собственному заголовку.");
    }
}

}

bool extractMeta(FileHead& file, ImageRecord& record) {
    record.path = file.path();
    record.name = file.path().filename().string();
    record.folder = file.path().parent_path().string();
    record.fileSize = file.size();
    record.extensionName = formatFromExtension(file.path());

    if (file.size() == 0) {
        record.check = CheckState::NotImage;
        record.format = Format::NotImage;
        record.problems.push_back("Файл пустой (0 байт)");
        addField(record, "Формат", "Сигнатура", "нет данных",
                 "В пустом файле нет ни одного байта, определить формат невозможно.");
        addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
                 "Пустой файл не может быть изображением.");
        addProblemFields(record);
        record.bytesRead = file.bytesRead();
        return false;
    }

    std::vector<std::uint8_t> probe;
    const bool read = file.readAt(0, kProbeBytes, probe);
    if (!read && probe.empty()) {
        record.check = CheckState::NotImage;
        record.format = Format::NotImage;
        record.problems.push_back("Не удалось прочитать начало файла");
        addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
                 "Файл недоступен для чтения.");
        addProblemFields(record);
        record.bytesRead = file.bytesRead();
        return false;
    }

    const Detection detection = detectFormat(probe.data(), probe.size());
    record.format = detection.format;
    for (std::size_t i = 0; i < std::min<std::size_t>(8, probe.size()); ++i) {
        record.signature[i] = probe[i];
    }

    if (!record.extensionName.empty() &&
        record.extensionName != formatShortName(detection.format)) {
        record.extensionMismatch = true;
    }

    if (detection.format == Format::NotImage) {
        record.check = CheckState::NotImage;
        std::string problem = "Сигнатура изображения не найдена";
        if (!record.extensionName.empty()) {
            problem += ", хотя расширение указывает на " + record.extensionName;
        }
        record.problems.push_back(problem);
        addField(record, "Формат", "Определён по сигнатуре", "нет",
                 "Первые 64 байта не совпали ни с одним из шести поддерживаемых форматов.");
        if (!record.extensionName.empty()) {
            addField(record, "Проверка", "Подозрение на подмену", "расширение " + record.extensionName,
                     "Файл с чужим расширением: содержимое разбирать нечем, а доверять расширению "
                     "нельзя.");
        }
        addField(record, "Проверка", "Состояние файла", checkStateName(record.check),
                 "Файл не распознан как изображение.");
        addProblemFields(record);
        record.bytesRead = file.bytesRead();
        return false;
    }

    bool ok = false;
    switch (detection.format) {
        case Format::Bmp:
            ok = parseBmp(file, record);
            break;
        case Format::Png:
            ok = parsePng(file, record);
            break;
        case Format::Jpeg:
            ok = parseJpeg(file, record);
            break;
        case Format::Gif:
            ok = parseGif(file, record);
            break;
        case Format::Tiff:
            ok = parseTiff(file, record);
            break;
        case Format::Pcx:
            ok = parsePcx(file, record);
            break;
        default:
            break;
    }

    record.bytesRead = file.bytesRead();
    if (!ok && record.check == CheckState::Ok) {
        record.check = CheckState::Corrupted;
    }
    if (record.check == CheckState::Ok && record.extensionMismatch) {
        record.check = CheckState::Warning;
        record.problems.push_back("Расширение файла не соответствует содержимому");
    }

    addCommonFields(record, detection);
    addField(record, "Формат", "Путь", record.folder.empty() ? record.name : record.folder + "/" + record.name,
             "Полный путь, по которому файл был открыт для чтения.");
    addField(record, "Формат", "Размер файла", humanSize(record.fileSize) + " (" +
                                   text(static_cast<unsigned long long>(record.fileSize)) + " Б)",
             "Полный размер файла; для растровых данных читается только начало.");
    addField(record, "Формат", "Прочитано байт", humanSize(record.bytesRead),
             "Ленивое чтение: загружаются только байты заголовков и маркеров, а не всё изображение.");
    addProblemFields(record);
    return true;
}

}