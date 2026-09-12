#include "ClassBot.h"
#include "Modules/ModuleManager.h"

class FClassBotGameModule : public FDefaultGameModuleImpl
{
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		// Force load PptxViewer plugin module at startup
		FModuleManager::Get().LoadModule("PptxViewer");
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FClassBotGameModule, ClassBot, "ClassBot");