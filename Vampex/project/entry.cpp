#include <pch/pch.hpp>

#include <utilities/logging/logging.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/bootstrap/bootstrap.hpp>
#include <utilities/steam/steam.hpp>

#include <core/hooks/hooks.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>
#include <protection/protection.hpp>

// ---------------------------------------------------------------------------
// DEV ONLY: Bypass PROTECTION_CHECK so the DLL does not intentionally crash
// when the game build does not match the pinned build (e.g. 14184).
// Remove or comment this out before shipping a release build.
// ---------------------------------------------------------------------------
#if defined( DEV )
#undef PROTECTION_CHECK
#define PROTECTION_CHECK() true
#endif

namespace {

#if defined( DEV )
	void init_log (const char* msg) {
		logging::file::write("ERROR", msg);
	}

#define INIT_FAIL( msg ) \
		do { \
			init_log( msg ); \
			return 0; \
		} while ( 0 )

#define INIT_WARN( msg ) logging::file::write("WARN", msg)
#else
#define INIT_FAIL( msg ) \
		do { \
			logging::file::write("ERROR", msg); \
			MessageBoxA( nullptr, xs( msg ), xs( "..." ), MB_ICONERROR ); \
			return 0; \
		} while ( 0 )

#define INIT_WARN( msg ) INIT_FAIL( msg )
#endif

#define INIT_STAGE(name, expression, failure) \
		do { \
			logging::file::write("STAGE", name " begin"); \
			if (!(expression)) INIT_FAIL(failure); \
			logging::file::write("STAGE", name " ok"); \
		} while (0)

	DWORD WINAPI init_thread (LPVOID param) {
		const auto module_handle = static_cast<HMODULE>(param);
		const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (!logging::file::initialize()) {
			logging::file::write("WARN", "file logging unavailable; DebugView output remains active");
		}
		logging::file::write("INFO", "initialization thread started");
		const auto module_message = std::format("Vampex module base: {:#x}",
			reinterpret_cast<std::uintptr_t>(module_handle));
		logging::file::write("INFO", module_message.c_str());
		if (FAILED(com_result)) logging::file::write("WARN", "COM initialization failed");

#if !defined( DEV )
		// bool protection_result = false; g_protection.attach( &protection_result );
		// if ( !protection_result )
		// {
		// 	g_protection.crash( );
		// 	return 0;
		// }
#endif

		logging::file::write("STAGE", "config initialization begin");
		config::initialize ();
		logging::file::write("STAGE", "config initialization ok");
		logging::file::write("STAGE", "settings bind finalization begin");
		settings::finalize_binds ();
		logging::file::write("STAGE", "settings bind finalization ok");

		logging::file::write("STAGE", "bootstrap begin");
		bootstrap::on_dll_attach (module_handle);
		logging::file::write("STAGE", "bootstrap ok");

		{
			logging::file::write("STAGE", "console logging begin");
			if (!logging::console::initialize ()) {
#if defined( DEV )
				INIT_WARN ("failed to initialize console logging.");
#else
				INIT_FAIL ("failed to initialize console logging.");
#endif
			}
			else logging::file::write("STAGE", "console logging ok");

			// No current feature calls popup::show. Its RTTI lookup is unnecessary
			// during startup and depends on game internals that can change.
			logging::file::write("INFO", "unused popup logging initialization skipped");
		}

		{

			INIT_STAGE("thread pool", PROTECTION_CHECK () && threadpool::initialize (), "failed to initialize thread pool.");

			INIT_STAGE("steam http", PROTECTION_CHECK () && steam::http::initialize (), "failed to initialize steam http.");

			INIT_STAGE("steam friends", PROTECTION_CHECK () && steam::friends::initialize (), "failed to initialize steam friends.");

			INIT_STAGE("steam utils", PROTECTION_CHECK () && steam::utils::initialize (), "failed to initialize steam utils.");
		}

		{
			INIT_STAGE("module addresses", PROTECTION_CHECK () && addresses::modules::initialize (), "failed to initialize module addresses.");

			INIT_STAGE("global addresses", PROTECTION_CHECK () && addresses::globals::initialize (), "failed to initialize global addresses.");

			INIT_STAGE("function addresses", PROTECTION_CHECK () && addresses::functions::initialize (), "failed to initialize function addresses.");
		}

		{
			INIT_STAGE("materials system", PROTECTION_CHECK () && systems::materials::initialize (), "failed to initialize materials system.");

			INIT_STAGE("event system", PROTECTION_CHECK () && systems::events::initialize (), "failed to initialize event system.");

			INIT_STAGE("vpk parse system", PROTECTION_CHECK () && systems::g_icons.initialize (), "failed to initialize vpk parse system.");

			INIT_STAGE("model preview system", PROTECTION_CHECK () && systems::g_model_preview.initialize (), "failed to initialize model preview system.");
		}

		{
			INIT_STAGE("econ item system", PROTECTION_CHECK () && features::changer::g_econ_item_system.initialize (), "failed to initialize econ item system.");
		}

		{

			INIT_STAGE("utility hooks", PROTECTION_CHECK () && hooks::utility::initialize (), "failed to initialize utility hooks.");

			INIT_STAGE("cheat hooks", PROTECTION_CHECK () && hooks::cheat::initialize (), "failed to initialize cheat hooks.");
		}

		{
			INIT_STAGE("cvar unlock", PROTECTION_CHECK () && addresses::globals::cvar->unlock_all (), "failed to unlock hidden cvars.");
		}

		logging::file::write("STAGE", "skybox discovery begin");
		features::world::g_scene.discover_skyboxes ();
		logging::file::write("STAGE", "skybox discovery ok");
		logging::file::write("INFO", "initialization complete");
		return 1;
	}

} // namespace

extern "C" int __stdcall entry (HMODULE module_handle, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		_CRT_INIT (module_handle, reason, reserved);
		DisableThreadLibraryCalls (module_handle);

		const auto thread = CreateThread (nullptr, 0, init_thread, module_handle, 0, nullptr);
		if (!thread) {
			return 0;
		}

		CloseHandle (thread);
		return 1;
	}
#if defined( DEV )
	else if (reason == DLL_PROCESS_DETACH) {
		logging::file::write("INFO", "DLL_PROCESS_DETACH received");
		_CRT_INIT (module_handle, reason, reserved);
		features::esp::player::g_chams.bt ().shutdown ();
		features::esp::player::g_chams.os ().shutdown ();

		features::world::g_weather.release ();
		rendering::g_menu.shutdown ();

		systems::events::shutdown ();
		hooks::utility::shutdown ();
		hooks::cheat::shutdown ();
		CoUninitialize ();
		logging::file::shutdown();
	}
#endif

	return 1;
}
