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

void generate_options(bpo::options_description *app, bpo::variables_map &vm, int argc, const char **argv, HelpMessageMode mode)
{
    bpo::options_description demo("for test cmd parser");
    demo.add_options()
            ("integer", bpo::value<int>(), "integer test")
            ("bool", bpo::value<bool>(), "bool test")
            ("string", bpo::value<string>(), "string test")
            ("vector_string", bpo::value< vector<string> >(), "string array test");
    app->add(demo);
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

    bpo::options_description *app = new bpo::options_description("");
    gArgs.InitPromOptions(generate_options, app, vecChar.size(), &vecChar[0], HMM_EMPTY);
}

BOOST_AUTO_TEST_CASE(boolarg)
{
    ResetArgs("-foo");
    BOOST_CHECK(gArgs.GetArg("-foo", false));
    BOOST_CHECK(gArgs.GetArg("-foo", true));

    BOOST_CHECK(!gArgs.GetArg("-fo", false));
    BOOST_CHECK(gArgs.GetArg("-fo", true));

    BOOST_CHECK(!gArgs.GetArg("-fooo", false));
    BOOST_CHECK(gArgs.GetArg("-fooo", true));

    ResetArgs("-foo=0");
    BOOST_CHECK(!gArgs.GetArg("-foo", false));
    BOOST_CHECK(!gArgs.GetArg("-foo", true));

    ResetArgs("-foo=1");
    BOOST_CHECK(gArgs.GetArg("-foo", false));
    BOOST_CHECK(gArgs.GetArg("-foo", true));

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs("-nofoo");
    BOOST_CHECK(!gArgs.GetArg("-foo", false));
    BOOST_CHECK(!gArgs.GetArg("-foo", true));

    ResetArgs("-nofoo=1");
    BOOST_CHECK(!gArgs.GetArg("-foo", false));
    BOOST_CHECK(!gArgs.GetArg("-foo", true));

    ResetArgs("-foo -nofoo");  // -nofoo should win
    BOOST_CHECK(!gArgs.GetArg("-foo", false));
    BOOST_CHECK(!gArgs.GetArg("-foo", true));

    ResetArgs("-foo=1 -nofoo=1");  // -nofoo should win
    BOOST_CHECK(!gArgs.GetArg("-foo", false));
    BOOST_CHECK(!gArgs.GetArg("-foo", true));

    ResetArgs("-foo=0 -nofoo=0");  // -nofoo=0 should win
    BOOST_CHECK(gArgs.GetArg("-foo", false));
    BOOST_CHECK(gArgs.GetArg("-foo", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs("--foo=1");
    BOOST_CHECK(gArgs.GetArg("-foo", false));
    BOOST_CHECK(gArgs.GetArg("-foo", true));

    ResetArgs("--nofoo=1");
    BOOST_CHECK(!gArgs.GetArg("-foo", false));
    BOOST_CHECK(!gArgs.GetArg("-foo", true));

}

BOOST_AUTO_TEST_CASE(stringarg)
{
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)"eleven"), "eleven");

    ResetArgs("-foo -bar");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)"eleven"), "");

    ResetArgs("-foo=");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)"eleven"), "");

    ResetArgs("-foo=11");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)""), "11");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)"eleven"), "11");

    ResetArgs("-foo=eleven");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)""), "eleven");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)"eleven"), "eleven");

}

BOOST_AUTO_TEST_CASE(intarg)
{
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 11), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 0), 0);

    ResetArgs("-foo -bar");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 11), 0);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 0);

    ResetArgs("-foo=11 -bar=12");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 0), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 12);

    ResetArgs("-foo=NaN -bar=NotANumber");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 1), 0);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 0);
}

BOOST_AUTO_TEST_CASE(doubledash)
{
    ResetArgs("--foo");
    BOOST_CHECK_EQUAL(gArgs.GetArg("foo", false), true);

    ResetArgs("--foo=verbose --bar=1");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", (std::string)""), "verbose");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 0), 1);
}

BOOST_AUTO_TEST_CASE(boolargno)
{
    ResetArgs("-nofoo");
    BOOST_CHECK(!gArgs.GetArg("-foo", true));
    BOOST_CHECK(!gArgs.GetArg("-foo", false));

    ResetArgs("-nofoo=1");
    BOOST_CHECK(!gArgs.GetArg("-foo", true));
    BOOST_CHECK(!gArgs.GetArg("-foo", false));

    ResetArgs("-nofoo=0");
    BOOST_CHECK(gArgs.GetArg("-foo", true));
    BOOST_CHECK(gArgs.GetArg("-foo", false));

    ResetArgs("-foo --nofoo"); // --nofoo should win
    BOOST_CHECK(!gArgs.GetArg("-foo", true));
    BOOST_CHECK(!gArgs.GetArg("-foo", false));

    ResetArgs("-nofoo -foo"); // foo always wins:
    BOOST_CHECK(gArgs.GetArg("-foo", true));
    BOOST_CHECK(gArgs.GetArg("-foo", false));
}

BOOST_AUTO_TEST_SUITE_END()
