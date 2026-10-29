#include "Server.h"
#include <iostream>
#include "Сrypto.h"
#include "FileCrypto.h"
#include "Logger.h"
#include "SessionRepository.h"
#include "Config.h"
#include "AuthController.h"
#include "SessionCleaner.h"
#include "ChatRepository.h"
#include "ChatController.h"
#include "ChatService.h" 
#include "UploadCleaner.h"
#include "Periodictask.h"


int main()
{

    Logger::instance().init("server.log");
    LOG_INFO("Server starting...");
    Config config = Config::load("config.json");
    Router router;

    ConnectionPool db_pool(
        config.database.pool_size,
        config.database.to_connection_string()
    );
    SessionRepository session_repository(db_pool, std::chrono::minutes(30), std::chrono::hours(12));
    
    std::string crypto_error;
    if (crypto().init_from_env(crypto_error))
        std::cout << "Crypto: шифрование включено, активный ключ " << crypto().active_key_id() << std::endl;
    else
        std::cout << "Crypto: ШИФРОВАНИЕ ВЫКЛЮЧЕНО (" << crypto_error << "). Данные пишутся открытым текстом." << std::endl;
    // перед боевым запуском замените на: return 1;


    SessionCleaner session_cleaner(session_repository);
    ChatRepository cleaner_repo(db_pool);
    GroupRepository groups(db_pool);
    ThreadPool pool(4);
    ChatService chat(
        db_pool,
        session_repository,
        pool,
        { "https://localhost:8080/" }
    );


    
    ChatController chat_controller(db_pool, session_repository);
    chat_controller.register_routes(router);
    chat_controller.on_member_removed = [&](int gid, int uid) { chat.kick_from_group_call(gid, uid); };
    chat_controller.on_group_deleted = [&](int gid) { chat.end_group_call(gid, "group_deleted"); };// до запуска сервера
    AuthController auth_controller(db_pool, session_repository,chat);
    auth_controller.register_routes(router);
    UploadCleaner cleaner_plain(cleaner_repo, "uploads/chat");
    UploadCleaner cleaner_enc(cleaner_repo, "uploads_enc/chat");
    cleaner_plain.start();
    cleaner_enc.start();
    PeriodicTask call_sweeper(std::chrono::seconds(5), [&]() { chat.sweep_calls(); });
    call_sweeper.start();

    if (crypto().enabled())
    {
        long long total = 0;
        int n = 0;
        while ((n = cleaner_repo.encrypt_legacy_messages(200)) > 0)  total += n;
        while ((n = groups.encrypt_legacy_messages(200)) > 0) total += n;

        const int files = migrate_legacy_files();
        std::cout << "Crypto: зашифровано сообщений " << total << ", файлов " << files << std::endl;
    }

    try
    {
        boost::asio::io_context io;

        Server server(io, 8765, pool, router,chat);

        std::cout << "Server started on port 8080\n";

        io.run();
        /*pool.shutdown();*/
    }
    catch (std::exception& e)
    {
        std::cout << "Error: " << e.what() << std::endl;
    }
}