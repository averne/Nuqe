#include <chrono>
#include <switch.h>

#include "error.hpp"
#include "mtp_storage.hpp"
#include "mtp_server.hpp"
#include "usb.hpp"
#include "utils.hpp"

extern "C" void userAppInit() {
    nq::log::initialize();
}

extern "C" void userAppExit() {
    nq::log::finalize();
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    MemoryInfo mem_info; u32 page_info;
    svcQueryMemory(&mem_info, &page_info, ctx->pc.x);
    FATAL("%#x exception with pc=%#lx\n", ctx->error_desc, ctx->pc.x - mem_info.addr);
}

using namespace std::chrono_literals;

void exit_thread_func(bool *should_exit) {
    while (appletMainLoop()) {
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS)
            break;
        std::this_thread::sleep_for(10ms);
    }
    nq::usb::cancel();
    *should_exit = true;
}

int main(int argc, char **argv) {
#ifndef DEBUG
    consoleInit(nullptr);
    puts("Nuqe " VERSION "-" COMMIT);
    puts("Press + to exit");
    consoleUpdate(nullptr);
#endif

    INFO("Starting\n");

    R_TRY_LOG(nq::usb::initialize());

    auto sd_storage = nq::mtp::Storage(
        nq::fs::Filesystem::sdmc(),
        {1, 1},
        {
            .storage_type      = nq::mtp::StorageType::RemovableRam,
            .filesystem_type   = nq::mtp::FilesystemType::GenericHierachical,
            .access_capability = nq::mtp::AccessCapability::ReadWrite,
            .description       = u"sd",
        }
    );

    auto user_storage = nq::mtp::Storage(
        FsBisPartitionId_User,
        {2, 1},
        {
            .storage_type      = nq::mtp::StorageType::FixedRam,
            .filesystem_type   = nq::mtp::FilesystemType::GenericHierachical,
            .access_capability = nq::mtp::AccessCapability::ReadOnlyNoDeletion,
            .description       = u"user",
        }
    );

    auto system_storage = nq::mtp::Storage(
        FsBisPartitionId_System,
        {2, 2},
        {
            .storage_type      = nq::mtp::StorageType::FixedRam,
            .filesystem_type   = nq::mtp::FilesystemType::GenericHierachical,
            .access_capability = nq::mtp::AccessCapability::ReadOnlyNoDeletion,
            .description       = u"system",
        }
    );

    auto calibration_storage = nq::mtp::Storage(
        FsBisPartitionId_CalibrationFile,
        {2, 3},
        {
            .storage_type      = nq::mtp::StorageType::FixedRam,
            .filesystem_type   = nq::mtp::FilesystemType::GenericHierachical,
            .access_capability = nq::mtp::AccessCapability::ReadOnlyNoDeletion,
            .description       = u"calibration",
        }
    );

    nq::mtp::StorageManager man;
    man.add_storage(std::move(sd_storage));
    man.add_storage(std::move(user_storage));
    man.add_storage(std::move(system_storage));
    man.add_storage(std::move(calibration_storage));

    auto server = nq::mtp::Server(man);

    bool should_exit = false;
    auto exit_thread = std::thread(exit_thread_func, &should_exit);

    while (!should_exit) {
        // Wait for usb connection
        if (!nq::usb::wait_ready(100ms))
            continue;

        if (auto rc = server.process(); rc)
            TRACE("Successfully processed request\n\n");
        else if (rc != nq::err::KernelTimedOut)
            ERROR("Failed to process request: %#x\n\n", rc);
    }

    INFO("Exiting\n");
    nq::usb::finalize();
    exit_thread.join();

#ifndef DEBUG
    consoleExit(nullptr);
#endif

    return 0;
}
