// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"
#include "test/test_bitcoin.h"

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/program_options.hpp>

using std::string;
using std::vector;
namespace bpo = boost::program_options;

BOOST_FIXTURE_TEST_SUITE(getarg_tests, BasicTestingSetup)

static bpo::options_description *app = nullptr;

void generate_options(bpo::options_description *app, bpo::variables_map &vm, int argc, const char **argv, HelpMessageMode mode)
{
    bpo::options_description demo("for test cmd parser");
    demo.add_options()
            ("integer", bpo::value<int>(), "integer test")
            ("bool_string", bpo::value<string>(), "bool test")
            ("string", bpo::value<string>(), "string test")
            ("vector_string", bpo::value< vector<string> >(), "string array test");
    app->add(demo);
    bpo::store(bpo::parse_command_line(argc, argv, *app), vm);
}

static void ResetArgs(const std::string &strArg)
{
    std::vector<std::string> vecArg;
    if (strArg.size())
      boost::split(vecArg, strArg, boost::is_space(), boost::token_compress_on);

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "testbitcoin");

    // Convert to char*:
    std::vector<const char*> vecChar;
    for (std::string& s : vecArg)
        vecChar.push_back(s.c_str());

    // options_description can`t be cleared
    if(!app)
    {
        delete app;
        app = nullptr;
    }

    app = new bpo::options_description("");
    gArgs.InitPromOptions(generate_options, app, vecChar.size(), &vecChar[0], HMM_EMPTY);
}

BOOST_AUTO_TEST_CASE(boolarg)
{
    ResetArgs("--bool_string yes");
    BOOST_CHECK(gArgs.GetArg("-bool_string", false));
    BOOST_CHECK(gArgs.GetArg("-bool_string", true));

    ResetArgs("--bool_string no");
    BOOST_CHECK(!gArgs.GetArg("-bool_string", false));
    BOOST_CHECK(!gArgs.GetArg("-bool_string", true));

    ResetArgs("");
    BOOST_CHECK(!gArgs.GetArg("-bool_string", false));
    BOOST_CHECK(gArgs.GetArg("-bool_string", true));
}

BOOST_AUTO_TEST_CASE(stringarg)
{
    ResetArgs("--string str");
    BOOST_CHECK_NE(gArgs.GetArg("-string", (std::string)""), "");
    BOOST_CHECK_NE(gArgs.GetArg("-string", (std::string)"eleven"), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-string", (std::string)""), "str");

    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-string", (std::string)""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-string", (std::string)"eleven"), "eleven");
    BOOST_CHECK_NE(gArgs.GetArg("-string", (std::string)""), "eleven");
}

BOOST_AUTO_TEST_CASE(intarg)
{
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-integer", 11), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-integer", 0), 0);

    ResetArgs("--integer 0");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-integer", 11), 0);
    BOOST_CHECK_NE(gArgs.GetArg("-integer", 11), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-integer", 0), 0);
}

BOOST_AUTO_TEST_CASE(doubledash)
{
    ResetArgs("--bool_string yes");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bool_string", false), true);

    ResetArgs("--string verbose --integer 1");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-string", (std::string)""), "verbose");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-integer", 0), 1);

    delete app;
}

BOOST_AUTO_TEST_SUITE_END()
