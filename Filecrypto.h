#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "Atachments.h"

#include "Сrypto.h"


// Зашифрованные файлы лежат в отдельной папке под теми же именами, что и обычные.
// Старые незашифрованные остаются в uploads/chat, пока не будут перенесены.
inline const std::string kPlainDir = "uploads/chat";
inline const std::string kEncDir = "uploads_enc/chat";

inline std::optional<std::string> read_all(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return std::nullopt;
    return data;
}

// Пишет во временный файл и переименовывает: при сбое не остаётся наполовину записанный файл
inline bool write_all(const std::string& path, const std::string& data)
{
    namespace fs = std::filesystem;
    const std::string tmp = path + ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return false;
        out.write(data.data(), (std::streamsize)data.size());
        out.close();
        if (!out) { std::error_code rm; fs::remove(tmp, rm); return false; }
    }

    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) { std::error_code rm; fs::remove(tmp, rm); return false; }
    return true;
}

inline std::string enc_path_for(const std::string& stored_name) { return kEncDir + "/" + stored_name; }
inline std::string plain_path_for(const std::string& stored_name) { return kPlainDir + "/" + stored_name; }

inline bool encrypted_upload_exists(const std::string& stored_name)
{
    std::error_code ec;
    return std::filesystem::exists(enc_path_for(stored_name), ec);
}

// Содержимое зашифрованного файла в открытом виде. nullopt: файла нет, повреждён или подменён.
inline std::optional<std::string> read_encrypted_upload(const std::string& stored_name)
{
    auto blob = read_all(enc_path_for(stored_name));
    if (!blob) return std::nullopt;
    return crypto().decrypt(*blob, aad_file(stored_name));
}

// Сохраняет новый файл зашифрованным. false: шифрование или запись не удались.
inline bool save_encrypted_upload(const std::string& stored_name, const char* data, size_t size)
{
    std::error_code ec;
    std::filesystem::create_directories(kEncDir, ec);
    if (ec) return false;

    auto blob = crypto().encrypt(std::string(data, size), aad_file(stored_name));
    if (!blob) return false;

    return write_all(enc_path_for(stored_name), *blob);
}

// Переносит старые файлы в зашифрованную папку. Возвращает, сколько файлов перенесено.
// Исходный файл удаляется только после успешной записи.
inline int migrate_legacy_files()
{
    namespace fs = std::filesystem;
    if (!crypto().enabled()) return 0;

    std::error_code ec;
    fs::create_directories(kEncDir, ec);

    int done = 0;

    for (fs::directory_iterator it(kPlainDir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec)) continue;

        const std::string name = it->path().filename().string();
        if (!is_valid_attachment_url("/uploads/chat/" + name)) continue;     // чужие файлы не трогаем

        auto data = read_all(it->path().string());
        if (!data) continue;

        auto blob = crypto().encrypt(*data, aad_file(name));
        if (!blob) continue;

        if (!write_all(enc_path_for(name), *blob)) continue;

        std::error_code rm;
        fs::remove(it->path(), rm);
        ++done;
    }

    return done;
}