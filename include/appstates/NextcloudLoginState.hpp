#pragma once

#include "StateManager.hpp"
#include "appstates/BaseState.hpp"
#include "remote/Nextcloud.hpp"
#include "sys/threadpool.hpp"
#include "util/QrCode.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

/// @brief Interactive Nextcloud Login Flow v2 state.
class NextcloudLoginState final : public BaseState
{
    public:
        explicit NextcloudLoginState(std::string server);

        static inline std::shared_ptr<NextcloudLoginState> create(std::string server)
        {
            return std::make_shared<NextcloudLoginState>(std::move(server));
        }

        static inline std::shared_ptr<NextcloudLoginState> create_and_push(std::string server)
        {
            auto state = NextcloudLoginState::create(std::move(server));
            StateManager::push_state(state);
            return state;
        }

        void update() override;
        void render() override;

    private:
        enum class Phase
        {
            Starting,
            QrReady,
            Polling,
            Success,
            Failed,
            Cancelled
        };

        struct LoginData final : public sys::threadpool::DataStruct
        {
            explicit LoginData(std::string serverIn)
                : server(std::move(serverIn)) {}

            std::atomic<Phase> phase{Phase::Starting};
            std::atomic_bool cancelled{};
            std::mutex mutex{};
            std::string server{};
            std::string status{};
            remote::NextcloudLoginSession session{};
            remote::NextcloudResult result{remote::NextcloudResult::Ok};
        };

        std::shared_ptr<LoginData> m_data{};
        util::QrCode m_qr{};
        bool m_qrReady{};
        bool m_pollStarted{};

        static void begin_login(sys::threadpool::JobData data);
        static void finish_login(sys::threadpool::JobData data);

        void start_qr_polling();
        void finish_state(bool success);
};
