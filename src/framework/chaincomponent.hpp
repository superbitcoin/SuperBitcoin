#pragma once
//
// Created by root1 on 18-1-25.
//
#include "component.hpp"

using namespace appbase;
struct database { };

class CChainCommonent : public CComponent<CChainCommonent>
{
public:
    CChainCommonent();
    ~CChainCommonent();
    virtual void SetProgramOptions( options_description& cli, options_description& cfg ) override
    {
        cfg.add_options()
                ("readonly", "open the database in read only mode")
                ("dbsize", boost::program_options::value<uint64_t>()->default_value( 8*1024 ), "Minimum size MB of database shared memory file")
                ;
        cli.add_options()
                ("replay", "clear chain database and replay all blocks" )
                ("reset", "clear chain database and block log" )
                ;
    }

    void ComponentInitialize( const variables_map& options );
    void ComponentStartup() ;
    void ComponentShutdown() ;

    database& db() { return _db; }
    const char* whoru(){ return "I am CChainCommonent\n";}

private:
    database _db;
};
