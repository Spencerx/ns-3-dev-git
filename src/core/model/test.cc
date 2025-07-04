/*
 * Copyright (c) 2009 University of Washington
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "test.h"

#include "abort.h"
#include "assert.h"
#include "config.h"
#include "des-metrics.h"
#include "log.h"
#include "rng-seed-manager.h"
#include "singleton.h"
#include "system-path.h"

#include <cmath>
#include <cstring>
#include <list>
#include <map>
#include <vector>

/**
 * @file
 * @ingroup testing
 * @brief ns3::TestCase, ns3::TestSuite, ns3::TestRunner implementations,
 */

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Test");

bool
TestDoubleIsEqual(const double x1, const double x2, const double epsilon)
{
    NS_LOG_FUNCTION(x1 << x2 << epsilon);
    int exponent;
    double delta;
    double difference;

    //
    // Find exponent of largest absolute value
    //
    {
        double max = (std::fabs(x1) > std::fabs(x2)) ? x1 : x2;
        std::frexp(max, &exponent);
    }

    //
    // Form a neighborhood of size  2 * delta
    //
    delta = std::ldexp(epsilon, exponent);
    difference = x1 - x2;

    return difference <= delta && difference >= -delta;
}

/**
 * @ingroup testingimpl
 * Container for details of a test failure.
 */
struct TestCaseFailure
{
    /**
     * Constructor.
     *
     * @param [in] _cond    The name of the condition being tested.
     * @param [in] _actual  The actual value returned by the test.
     * @param [in] _limit   The expected value.
     * @param [in] _message The associated message.
     * @param [in] _file    The source file.
     * @param [in] _line    The source line.
     */
    TestCaseFailure(std::string _cond,
                    std::string _actual,
                    std::string _limit,
                    std::string _message,
                    std::string _file,
                    int32_t _line);
    std::string cond;    /**< The name of the condition being tested. */
    std::string actual;  /**< The actual value returned by the test. */
    std::string limit;   /**< The expected value. */
    std::string message; /**< The associated message. */
    std::string file;    /**< The source file. */
    int32_t line;        /**< The source line. */
};

/**
 * Output streamer for TestCaseFailure.
 *
 * @param [in,out] os The output stream.
 * @param [in] failure The TestCaseFailure to print.
 * @returns The stream.
 */
std::ostream&
operator<<(std::ostream& os, const TestCaseFailure& failure)
{
    os << "    test=\"" << failure.cond << "\" actual=\"" << failure.actual << "\" limit=\""
       << failure.limit << "\" in=\"" << failure.file << ":" << failure.line << "\" "
       << failure.message;

    return os;
}

/**
 * @ingroup testingimpl
 * Container for results from a TestCase.
 */
struct TestCase::Result
{
    /** Constructor. */
    Result();

    /** Test running time. */
    SystemWallClockMs clock;
    /** TestCaseFailure records for each child. */
    std::vector<TestCaseFailure> failure;
    /** \c true if any child TestCases failed. */
    bool childrenFailed;
};

/**
 * @ingroup testingimpl
 * Container for all tests.
 * @todo Move TestRunnerImpl to separate file.
 */
class TestRunnerImpl : public Singleton<TestRunnerImpl>
{
  public:
    /** Constructor. */
    TestRunnerImpl();

    /**
     * Add a new top-level TestSuite.
     * @param [in] testSuite The new TestSuite.
     */
    void AddTestSuite(TestSuite* testSuite);
    /** @copydoc TestCase::MustAssertOnFailure() */
    bool MustAssertOnFailure() const;
    /** @copydoc TestCase::MustContinueOnFailure() */
    bool MustContinueOnFailure() const;
    /**
     * Check if this run should update the reference data.
     * @return \c true if we should update the reference data.
     */
    bool MustUpdateData() const;
    /**
     * Get the path to the root of the source tree.
     *
     * The root directory is defined by the presence of two files:
     * "VERSION" and "LICENSE".
     *
     * @returns The path to the root.
     */
    std::string GetTopLevelSourceDir() const;
    /**
     * Get the path to temporary directory.
     * @return The temporary directory path.
     */
    std::string GetTempDir() const;
    /** @copydoc TestRunner::Run() */
    int Run(int argc, char* argv[]);

  private:
    /**
     * Check if this is the root of the source tree.
     * @param [in] path The path to test.
     * @returns \c true if \pname{path} is the root.
     */
    bool IsTopLevelSourceDir(std::string path) const;
    /**
     * Clean up characters not allowed in XML.
     *
     * XML files have restrictions on certain characters that may be present in
     * data.  We need to replace these characters with their alternate
     * representation on the way into the XML file.
     *
     * Specifically, we make these replacements:
     *    Raw Source | Replacement
     *    :--------: | :---------:
     *    '<'        | "&lt;"
     *    '>'        | "&gt;"
     *    '&'        | "&amp;"
     *    '"'        | "&39;"
     *    '\'        | "&quot;"
     *
     * @param [in] xml The raw string.
     * @returns The sanitized string.
     */
    std::string ReplaceXmlSpecialCharacters(std::string xml) const;
    /**
     * Print the test report.
     *
     * @param [in] test The TestCase to print.
     * @param [in,out] os The output stream.
     * @param [in] xml Generate XML output if \c true.
     * @param [in] level Indentation level.
     */
    void PrintReport(TestCase* test, std::ostream* os, bool xml, int level);
    /**
     * Print the list of all requested test suites.
     *
     * @param [in] begin Iterator to the first TestCase to print.
     * @param [in] end Iterator to the end of the list.
     * @param [in] printTestType Prepend the test type label if \c true.
     */
    void PrintTestNameList(std::list<TestCase*>::const_iterator begin,
                           std::list<TestCase*>::const_iterator end,
                           bool printTestType) const;
    /** Print the list of test types. */
    void PrintTestTypeList() const;
    /**
     * Print the help text.
     * @param [in] programName The name of the invoking program.
     */
    void PrintHelp(const char* programName) const;
    /**
     * Generate the list of tests matching the constraints.
     *
     * Test name and type constraints are or'ed.  The duration constraint
     * is and'ed.
     *
     * @param [in] testName Include a specific test by name.
     * @param [in] testType Include all tests of give type.
     * @param [in] maximumTestDuration Restrict to tests shorter than this.
     * @returns The list of tests matching the filter constraints.
     */
    std::list<TestCase*> FilterTests(std::string testName,
                                     TestSuite::Type testType,
                                     TestCase::Duration maximumTestDuration);

    /** Container type for the test. */
    typedef std::vector<TestSuite*> TestSuiteVector;

    TestSuiteVector m_suites; //!< The list of tests.
    std::string m_tempDir;    //!< The temporary directory.
    bool m_verbose;           //!< Produce verbose output.
    bool m_assertOnFailure;   //!< \c true if we should assert on failure.
    bool m_continueOnFailure; //!< \c true if we should continue on failure.
    bool m_updateData;        //!< \c true if we should update reference data.
};

TestCaseFailure::TestCaseFailure(std::string _cond,
                                 std::string _actual,
                                 std::string _limit,
                                 std::string _message,
                                 std::string _file,
                                 int32_t _line)
    : cond(_cond),
      actual(_actual),
      limit(_limit),
      message(_message),
      file(_file),
      line(_line)
{
    NS_LOG_FUNCTION(this << _cond << _actual << _limit << _message << _file << _line);
}

TestCase::Result::Result()
    : childrenFailed(false)
{
    NS_LOG_FUNCTION(this);
}

TestCase::TestCase(std::string name)
    : m_parent(nullptr),
      m_dataDir(""),
      m_runner(nullptr),
      m_result(nullptr),
      m_name(name),
      m_duration(TestCase::Duration::QUICK)
{
    NS_LOG_FUNCTION(this << name);
}

TestCase::~TestCase()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_runner == nullptr);
    m_parent = nullptr;
    delete m_result;
    for (auto i = m_children.begin(); i != m_children.end(); ++i)
    {
        delete *i;
    }
    m_children.clear();
}

void
TestCase::AddTestCase(TestCase* testCase, TestCase::Duration duration)
{
    NS_LOG_FUNCTION(&testCase << duration);

    // Test names are used to create temporary directories,
    // so we test for illegal characters.
    //
    // Windows: <>:"/\|?*
    //   http://msdn.microsoft.com/en-us/library/aa365247(v=vs.85).aspx
    // Mac:     : (deprecated, was path separator in Mac OS Classic, pre X)
    // Unix:    / (and .. may give trouble?)
    //
    // The Windows list is too restrictive:  we like to label
    // tests with "val = v1 * v2" or "v1 < 3" or "case: foo --> bar"
    // So we allow ':<>*"

    std::string badchars = "\"/\\|?";
    // Badchar Class  Regex          Count of failing test names
    // All            ":<>\"/\\|?*"  611
    // Allow ':'      "<>\"/\\|?*"   128
    // Allow ':<>'    "\"/\\|?*"      12
    // Allow ':<>*'    "\"/\\|?"       0

    std::string::size_type badch = testCase->m_name.find_first_of(badchars);
    if (badch != std::string::npos)
    {
        /*
          To count the bad test names, use NS_LOG_UNCOND instead
          of NS_FATAL_ERROR, and the command
          $ ./ns3 run "test-runner --list" 2>&1 | grep "^Invalid" | wc
        */
        NS_LOG_UNCOND("Invalid test name: cannot contain any of '" << badchars
                                                                   << "': " << testCase->m_name);
    }

    testCase->m_duration = duration;
    testCase->m_parent = this;
    m_children.push_back(testCase);
}

bool
TestCase::IsFailed() const
{
    NS_LOG_FUNCTION(this);
    return m_result->childrenFailed || !m_result->failure.empty();
}

void
TestCase::Run(TestRunnerImpl* runner)
{
    NS_LOG_FUNCTION(this << runner);
    m_result = new Result();
    m_runner = runner;
    Config::Reset();
    DoSetup();
    m_result->clock.Start();
    for (auto i = m_children.begin(); i != m_children.end(); ++i)
    {
        RngSeedManager::ResetNextStreamIndex();
        TestCase* test = *i;
        test->Run(runner);
        if (IsFailed())
        {
            goto out;
        }
    }
    DoRun();
out:
    m_result->clock.End();
    DoTeardown();
    Config::Reset();
    m_runner = nullptr;
}

std::string
TestCase::GetName() const
{
    NS_LOG_FUNCTION(this);
    return m_name;
}

TestCase*
TestCase::GetParent() const
{
    return m_parent;
}

void
TestCase::ReportTestFailure(std::string cond,
                            std::string actual,
                            std::string limit,
                            std::string message,
                            std::string file,
                            int32_t line)
{
    NS_LOG_FUNCTION(this << cond << actual << limit << message << file << line);
    m_result->failure.emplace_back(cond, actual, limit, message, file, line);
    // set childrenFailed flag on parents.
    TestCase* current = m_parent;
    while (current != nullptr)
    {
        current->m_result->childrenFailed = true;
        current = current->m_parent;
    }
}

bool
TestCase::MustAssertOnFailure() const
{
    NS_LOG_FUNCTION(this);
    return m_runner->MustAssertOnFailure();
}

bool
TestCase::MustContinueOnFailure() const
{
    NS_LOG_FUNCTION(this);
    return m_runner->MustContinueOnFailure();
}

std::string
TestCase::CreateDataDirFilename(std::string filename)
{
    NS_LOG_FUNCTION(this << filename);
    const TestCase* current = this;
    while (current != nullptr && current->m_dataDir.empty())
    {
        current = current->m_parent;
    }
    if (current == nullptr)
    {
        NS_FATAL_ERROR("No one called SetDataDir prior to calling this function");
    }

    std::string a = SystemPath::Append(m_runner->GetTopLevelSourceDir(), current->m_dataDir);
    std::string b = SystemPath::Append(a, filename);
    return b;
}

std::string
TestCase::CreateTempDirFilename(std::string filename)
{
    NS_LOG_FUNCTION(this << filename);
    if (m_runner->MustUpdateData())
    {
        return CreateDataDirFilename(filename);
    }
    else
    {
        std::list<std::string> names;
        const TestCase* current = this;
        while (current != nullptr)
        {
            names.push_front(current->m_name);
            current = current->m_parent;
        }
        std::string tempDir = SystemPath::Append(m_runner->GetTempDir(),
                                                 SystemPath::Join(names.begin(), names.end()));
        tempDir = SystemPath::CreateValidSystemPath(tempDir);

        SystemPath::MakeDirectories(tempDir);
        return SystemPath::Append(tempDir, filename);
    }
}

bool
TestCase::IsStatusFailure() const
{
    NS_LOG_FUNCTION(this);
    return !IsStatusSuccess();
}

bool
TestCase::IsStatusSuccess() const
{
    NS_LOG_FUNCTION(this);
    return m_result->failure.empty();
}

void
TestCase::SetDataDir(std::string directory)
{
    NS_LOG_FUNCTION(this << directory);
    m_dataDir = directory;
}

void
TestCase::DoSetup()
{
    NS_LOG_FUNCTION(this);
}

void
TestCase::DoTeardown()
{
    NS_LOG_FUNCTION(this);
}

TestSuite::TestSuite(std::string name, TestSuite::Type type)
    : TestCase(name),
      m_type(type)
{
    NS_LOG_FUNCTION(this << name << type);
    TestRunnerImpl::Get()->AddTestSuite(this);
}

TestSuite::Type
TestSuite::GetTestType()
{
    NS_LOG_FUNCTION(this);
    return m_type;
}

void
TestSuite::DoRun()
{
    NS_LOG_FUNCTION(this);
}

TestRunnerImpl::TestRunnerImpl()
    : m_tempDir(""),
      m_assertOnFailure(false),
      m_continueOnFailure(true),
      m_updateData(false)
{
    NS_LOG_FUNCTION(this);
}

void
TestRunnerImpl::AddTestSuite(TestSuite* testSuite)
{
    NS_LOG_FUNCTION(this << testSuite);
    m_suites.push_back(testSuite);
}

bool
TestRunnerImpl::MustAssertOnFailure() const
{
    NS_LOG_FUNCTION(this);
    return m_assertOnFailure;
}

bool
TestRunnerImpl::MustContinueOnFailure() const
{
    NS_LOG_FUNCTION(this);
    return m_continueOnFailure;
}

bool
TestRunnerImpl::MustUpdateData() const
{
    NS_LOG_FUNCTION(this);
    return m_updateData;
}

std::string
TestRunnerImpl::GetTempDir() const
{
    NS_LOG_FUNCTION(this);
    return m_tempDir;
}

bool
TestRunnerImpl::IsTopLevelSourceDir(std::string path) const
{
    NS_LOG_FUNCTION(this << path);
    bool haveVersion = false;
    bool haveLicense = false;

    //
    // If there's a file named VERSION and a file named LICENSE in this
    // directory, we assume it's our top level source directory.
    //

    std::list<std::string> files = SystemPath::ReadFiles(path);
    for (auto i = files.begin(); i != files.end(); ++i)
    {
        if (*i == "VERSION")
        {
            haveVersion = true;
        }
        else if (*i == "LICENSE")
        {
            haveLicense = true;
        }
    }

    return haveVersion && haveLicense;
}

std::string
TestRunnerImpl::GetTopLevelSourceDir() const
{
    NS_LOG_FUNCTION(this);
    std::string self = SystemPath::FindSelfDirectory();
    std::list<std::string> elements = SystemPath::Split(self);
    while (!elements.empty())
    {
        std::string path = SystemPath::Join(elements.begin(), elements.end());
        if (IsTopLevelSourceDir(path))
        {
            return path;
        }
        elements.pop_back();
    }
    NS_FATAL_ERROR("Could not find source directory from self=" << self);
    return self;
}

//
// XML files have restrictions on certain characters that may be present in
// data.  We need to replace these characters with their alternate
// representation on the way into the XML file.
//
std::string
TestRunnerImpl::ReplaceXmlSpecialCharacters(std::string xml) const
{
    NS_LOG_FUNCTION(this << xml);
    typedef std::map<char, std::string> specials_map;
    specials_map specials;
    specials['<'] = "&lt;";
    specials['>'] = "&gt;";
    specials['&'] = "&amp;";
    specials['"'] = "&#39;";
    specials['\''] = "&quot;";

    std::string result;
    std::size_t length = xml.length();

    for (size_t i = 0; i < length; ++i)
    {
        char character = xml[i];

        auto it = specials.find(character);

        if (it == specials.end())
        {
            result.push_back(character);
        }
        else
        {
            result += it->second;
        }
    }
    return result;
}

/** Helper to indent output a specified number of steps. */
struct Indent
{
    /**
     * Constructor.
     * @param [in] level The number of steps.  A step is "  ".
     */
    Indent(int level);
    /** The number of steps. */
    int level;
};

Indent::Indent(int _level)
    : level(_level)
{
    NS_LOG_FUNCTION(this << _level);
}

/**
 * Output streamer for Indent.
 * @param [in,out] os The output stream.
 * @param [in] val The Indent object.
 * @returns The stream.
 */
std::ostream&
operator<<(std::ostream& os, const Indent& val)
{
    for (int i = 0; i < val.level; i++)
    {
        os << "  ";
    }
    return os;
}

void
TestRunnerImpl::PrintReport(TestCase* test, std::ostream* os, bool xml, int level)
{
    NS_LOG_FUNCTION(this << test << os << xml << level);
    if (test->m_result == nullptr)
    {
        // Do not print reports for tests that were not run.
        return;
    }
    // Report times in seconds, from ms timer
    const double MS_PER_SEC = 1000.;
    double real = test->m_result->clock.GetElapsedReal() / MS_PER_SEC;
    double user = test->m_result->clock.GetElapsedUser() / MS_PER_SEC;
    double system = test->m_result->clock.GetElapsedSystem() / MS_PER_SEC;

    std::streamsize oldPrecision = (*os).precision(3);
    *os << std::fixed;

    std::string statusString = test->IsFailed() ? "FAIL" : "PASS";
    if (xml)
    {
        *os << Indent(level) << "<Test>" << std::endl;
        *os << Indent(level + 1) << "<Name>" << ReplaceXmlSpecialCharacters(test->m_name)
            << "</Name>" << std::endl;
        *os << Indent(level + 1) << "<Result>" << statusString << "</Result>" << std::endl;
        *os << Indent(level + 1) << "<Time real=\"" << real << "\" user=\"" << user
            << "\" system=\"" << system << "\"/>" << std::endl;
        for (uint32_t i = 0; i < test->m_result->failure.size(); i++)
        {
            TestCaseFailure failure = test->m_result->failure[i];
            *os << Indent(level + 2) << "<FailureDetails>" << std::endl
                << Indent(level + 3) << "<Condition>" << ReplaceXmlSpecialCharacters(failure.cond)
                << "</Condition>" << std::endl
                << Indent(level + 3) << "<Actual>" << ReplaceXmlSpecialCharacters(failure.actual)
                << "</Actual>" << std::endl
                << Indent(level + 3) << "<Limit>" << ReplaceXmlSpecialCharacters(failure.limit)
                << "</Limit>" << std::endl
                << Indent(level + 3) << "<Message>" << ReplaceXmlSpecialCharacters(failure.message)
                << "</Message>" << std::endl
                << Indent(level + 3) << "<File>" << ReplaceXmlSpecialCharacters(failure.file)
                << "</File>" << std::endl
                << Indent(level + 3) << "<Line>" << failure.line << "</Line>" << std::endl
                << Indent(level + 2) << "</FailureDetails>" << std::endl;
        }
        for (uint32_t i = 0; i < test->m_children.size(); i++)
        {
            TestCase* child = test->m_children[i];
            PrintReport(child, os, xml, level + 1);
        }
        *os << Indent(level) << "</Test>" << std::endl;
    }
    else
    {
        *os << Indent(level) << statusString << " " << test->GetName() << " " << real << " s"
            << std::endl;
        if (m_verbose)
        {
            for (uint32_t i = 0; i < test->m_result->failure.size(); i++)
            {
                *os << Indent(level) << test->m_result->failure[i] << std::endl;
            }
            for (uint32_t i = 0; i < test->m_children.size(); i++)
            {
                TestCase* child = test->m_children[i];
                PrintReport(child, os, xml, level + 1);
            }
        }
    }

    (*os).unsetf(std::ios_base::floatfield);
    (*os).precision(oldPrecision);
}

void
TestRunnerImpl::PrintHelp(const char* program_name) const
{
    NS_LOG_FUNCTION(this << program_name);
    std::cout
        << "Usage: " << program_name << " [OPTIONS]" << std::endl
        << std::endl
        << "Options: " << std::endl
        << "  --help                 : print these options" << std::endl
        << "  --print-test-name-list : print the list of names of tests available" << std::endl
        << "  --list                 : an alias for --print-test-name-list" << std::endl
        << "  --print-test-types     : print the type of tests along with their names" << std::endl
        << "  --print-test-type-list : print the list of types of tests available" << std::endl
        << "  --print-temp-dir       : print name of temporary directory before running "
        << std::endl
        << "                           the tests" << std::endl
        << "  --test-type=TYPE       : process only tests of type TYPE" << std::endl
        << "  --test-name=NAME       : process only test whose name matches NAME" << std::endl
        << "  --suite=NAME           : an alias (here for compatibility reasons only) " << std::endl
        << "                           for --test-name=NAME" << std::endl
        << "  --assert-on-failure    : when a test fails, crash immediately (useful" << std::endl
        << "                           when running under a debugger" << std::endl
        << "  --stop-on-failure      : when a test fails, stop immediately" << std::endl
        << "  --fullness=FULLNESS    : choose the duration of tests to run: QUICK, " << std::endl
        << "                           EXTENSIVE, or TAKES_FOREVER, where EXTENSIVE " << std::endl
        << "                           includes QUICK and TAKES_FOREVER includes " << std::endl
        << "                           QUICK and EXTENSIVE (only QUICK tests are " << std::endl
        << "                           run by default)" << std::endl
        << "  --verbose              : print details of test execution" << std::endl
        << "  --xml                  : format test run output as xml" << std::endl
        << "  --tempdir=DIR          : set temp dir for tests to store output files" << std::endl
        << "  --datadir=DIR          : set data dir for tests to read reference files" << std::endl
        << "  --out=FILE             : send test result to FILE instead of standard output"
        << std::endl
        << "  --append=FILE          : append test result to FILE instead of standard output"
        << std::endl;
}

void
TestRunnerImpl::PrintTestNameList(std::list<TestCase*>::const_iterator begin,
                                  std::list<TestCase*>::const_iterator end,
                                  bool printTestType) const
{
    NS_LOG_FUNCTION(this << &begin << &end << printTestType);
    std::map<TestSuite::Type, std::string> label;

    label[TestSuite::Type::ALL] = "all                  ";
    label[TestSuite::Type::UNIT] = "unit                 ";
    label[TestSuite::Type::SYSTEM] = "system               ";
    label[TestSuite::Type::EXAMPLE] = "example-as-test      ";
    label[TestSuite::Type::PERFORMANCE] = "performance          ";

    for (auto i = begin; i != end; ++i)
    {
        auto test = dynamic_cast<TestSuite*>(*i);
        NS_ASSERT(test != nullptr);
        if (printTestType)
        {
            std::cout << label[test->GetTestType()];
        }
        std::cout << test->GetName() << std::endl;
    }
}

void
TestRunnerImpl::PrintTestTypeList() const
{
    NS_LOG_FUNCTION(this);
    std::cout << "  core:        Run all TestSuite-based tests (exclude examples)" << std::endl;
    std::cout << "  example-as-test:     Examples (to see if example programs run successfully)"
              << std::endl;
    std::cout
        << "  performance: Performance Tests (check to see if the system is as fast as expected)"
        << std::endl;
    std::cout << "  system:      System Tests (spans modules to check integration of modules)"
              << std::endl;
    std::cout << "  unit:        Unit Tests (within modules to check basic functionality)"
              << std::endl;
}

std::list<TestCase*>
TestRunnerImpl::FilterTests(std::string testName,
                            TestSuite::Type testType,
                            TestCase::Duration maximumTestDuration)
{
    NS_LOG_FUNCTION(this << testName << testType);
    std::list<TestCase*> tests;
    for (uint32_t i = 0; i < m_suites.size(); ++i)
    {
        TestSuite* test = m_suites[i];
        if (testType != TestSuite::Type::ALL && test->GetTestType() != testType)
        {
            // skip test
            continue;
        }
        if (!testName.empty() && test->GetName() != testName)
        {
            // skip test
            continue;
        }

        // Remove any test cases that should be skipped.
        for (auto j = test->m_children.begin(); j != test->m_children.end();)
        {
            TestCase* testCase = *j;

            // If this test case takes longer than the maximum test
            // duration that should be run, then don't run it.
            if (testCase->m_duration > maximumTestDuration)
            {
                // Free this test case's memory.
                delete *j;

                // Remove this test case from the test suite.
                j = test->m_children.erase(j);
            }
            else
            {
                // Only advance through the vector elements if this test
                // case wasn't deleted.
                ++j;
            }
        }

        // Add this test suite.
        tests.push_back(test);
    }
    return tests;
}

int
TestRunnerImpl::Run(int argc, char* argv[])
{
    NS_LOG_FUNCTION(this << argc << argv);
    std::string testName = "";
    std::string testTypeString = "";
    std::string out = "";
    std::string fullness = "";
    bool xml = false;
    bool append = false;
    bool printTempDir = false;
    bool printTestTypeList = false;
    bool printTestNameList = false;
    bool printTestTypeAndName = false;
    TestCase::Duration maximumTestDuration = TestCase::Duration::QUICK;
    char* progname = argv[0];

    char** argi = argv;
    ++argi;

    while (*argi != nullptr)
    {
        std::string arg = *argi;

        if (arg == "--assert-on-failure")
        {
            m_assertOnFailure = true;
        }
        else if (arg == "--stop-on-failure")
        {
            m_continueOnFailure = false;
        }
        else if (arg == "--verbose")
        {
            m_verbose = true;
        }
        else if (arg == "--print-temp-dir")
        {
            printTempDir = true;
        }
        else if (arg == "--update-data")
        {
            m_updateData = true;
        }
        else if (arg == "--print-test-name-list" || arg == "--list")
        {
            printTestNameList = true;
        }
        else if (arg == "--print-test-types")
        {
            printTestTypeAndName = true;
        }
        else if (arg == "--print-test-type-list")
        {
            printTestTypeList = true;
        }
        else if (arg == "--append")
        {
            append = true;
        }
        else if (arg == "--xml")
        {
            xml = true;
        }
        else if (arg.find("--test-type=") != std::string::npos)
        {
            testTypeString = arg.substr(arg.find_first_of('=') + 1);
        }
        else if (arg.find("--test-name=") != std::string::npos ||
                 arg.find("--suite=") != std::string::npos)
        {
            testName = arg.substr(arg.find_first_of('=') + 1);
        }
        else if (arg.find("--tempdir=") != std::string::npos)
        {
            m_tempDir = arg.substr(arg.find_first_of('=') + 1);
        }
        else if (arg.find("--out=") != std::string::npos)
        {
            out = arg.substr(arg.find_first_of('=') + 1);
        }
        else if (arg.find("--fullness=") != std::string::npos)
        {
            fullness = arg.substr(arg.find_first_of('=') + 1);

            // Set the maximum test length allowed.
            if (fullness == "QUICK")
            {
                maximumTestDuration = TestCase::Duration::QUICK;
            }
            else if (fullness == "EXTENSIVE")
            {
                maximumTestDuration = TestCase::Duration::EXTENSIVE;
            }
            else if (fullness == "TAKES_FOREVER")
            {
                maximumTestDuration = TestCase::Duration::TAKES_FOREVER;
            }
            else
            {
                // Wrong fullness option
                PrintHelp(progname);
                return 3;
            }
        }
        else
        {
            // Print the help if arg == "--help" or arg is an un-recognized command-line argument
            PrintHelp(progname);
            return 0;
        }
        argi++;
    }
    TestSuite::Type testType;
    if (testTypeString.empty() || testTypeString == "core")
    {
        testType = TestSuite::Type::ALL;
    }
    else if (testTypeString == "example")
    {
        testType = TestSuite::Type::EXAMPLE;
    }
    else if (testTypeString == "unit")
    {
        testType = TestSuite::Type::UNIT;
    }
    else if (testTypeString == "system")
    {
        testType = TestSuite::Type::SYSTEM;
    }
    else if (testTypeString == "performance")
    {
        testType = TestSuite::Type::PERFORMANCE;
    }
    else
    {
        std::cout << "Invalid test type specified: " << testTypeString << std::endl;
        PrintTestTypeList();
        return 1;
    }

    std::list<TestCase*> tests = FilterTests(testName, testType, maximumTestDuration);

    if (m_tempDir.empty())
    {
        m_tempDir = SystemPath::MakeTemporaryDirectoryName();
    }
    if (printTempDir)
    {
        std::cout << m_tempDir << std::endl;
    }
    if (printTestNameList)
    {
        PrintTestNameList(tests.begin(), tests.end(), printTestTypeAndName);
        return 0;
    }
    if (printTestTypeList)
    {
        PrintTestTypeList();
        return 0;
    }

    std::ostream* os;
    if (!out.empty())
    {
        std::ofstream* ofs;
        ofs = new std::ofstream();
        std::ios_base::openmode mode = std::ios_base::out;
        if (append)
        {
            mode |= std::ios_base::app;
        }
        else
        {
            mode |= std::ios_base::trunc;
        }
        ofs->open(out, mode);
        os = ofs;
    }
    else
    {
        os = &std::cout;
    }

    // let's run our tests now.
    bool failed = false;
    if (tests.empty())
    {
        std::cerr << "Error:  no tests match the requested string" << std::endl;
        return 1;
    }
    else if (tests.size() > 1)
    {
        std::cerr << "Error:  tests should be launched separately (one at a time)" << std::endl;
        return 1;
    }

    for (auto i = tests.begin(); i != tests.end(); ++i)
    {
        TestCase* test = *i;

#ifdef ENABLE_DES_METRICS
        {
            /*
              Reorganize argv
              Since DES Metrics uses argv[0] for the trace file name,
              grab the test name and put it in argv[0],
              with test-runner as argv[1]
              then the rest of the original arguments.
            */
            std::string testname = test->GetName();
            std::string runner = "[" + SystemPath::Split(argv[0]).back() + "]";

            std::vector<std::string> desargs;
            desargs.push_back(testname);
            desargs.push_back(runner);
            for (int i = 1; i < argc; ++i)
            {
                desargs.push_back(argv[i]);
            }

            DesMetrics::Get()->Initialize(desargs, m_tempDir);
        }
#endif

        test->Run(this);
        PrintReport(test, os, xml, 0);
        if (test->IsFailed())
        {
            failed = true;
            if (!m_continueOnFailure)
            {
                return 1;
            }
        }
    }

    if (!out.empty())
    {
        delete os;
    }

    return failed ? 1 : 0;
}

int
TestRunner::Run(int argc, char* argv[])
{
    NS_LOG_FUNCTION(argc << argv);
    return TestRunnerImpl::Get()->Run(argc, argv);
}

std::ostream&
operator<<(std::ostream& os, TestSuite::Type type)
{
    switch (type)
    {
    case TestSuite::Type::ALL:
        return os << "ALL";
    case TestSuite::Type::UNIT:
        return os << "UNIT";
    case TestSuite::Type::SYSTEM:
        return os << "SYSTEM";
    case TestSuite::Type::EXAMPLE:
        return os << "EXAMPLE";
    case TestSuite::Type::PERFORMANCE:
        return os << "PERFORMANCE";
    };
    return os << "UNKNOWN(" << static_cast<uint32_t>(type) << ")";
}

std::ostream&
operator<<(std::ostream& os, TestCase::Duration duration)
{
    switch (duration)
    {
    case TestCase::Duration::QUICK:
        return os << "QUICK";
    case TestCase::Duration::EXTENSIVE:
        return os << "EXTENSIVE";
    case TestCase::Duration::TAKES_FOREVER:
        return os << "TAKES_FOREVER";
    };
    return os << "UNKNOWN(" << static_cast<uint32_t>(duration) << ")";
}

} // namespace ns3
