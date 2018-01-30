#include "netcomponent.hpp"
#include "chaincomponent.hpp"
CNetComponent::CNetComponent()
{
}

CNetComponent::~CNetComponent()
{
}

void CNetComponent::ComponentInitialize()
{
//    if( options.count( "config" ) )
//    {
//        auto config_file_name = options["config"].as<bfs::path>();
//        std::cout << "config name" << config_file_name << std::endl;
//    }
//    std::cout << "initialize net component\n";
//    CChainCommonent& chain = (CChainCommonent&)CBase::Instance().GetComponent("CChainCommonent");
//    std::cout << chain.whoru();
}
void CNetComponent::ComponentStartup()
{
    std::cout << "starting net component \n";
}
void CNetComponent::ComponentShutdown()
{
    std::cout << "shutdown net component \n";
}