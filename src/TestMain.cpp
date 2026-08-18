#include <iostream>
#include <juce_core/juce_core.h>

namespace
{
    class ConsoleLogger : public juce::Logger
    {
    public:
        void logMessage (const juce::String& message) override
        {
            std::cout << message << std::endl;
        }
    };
}

int main (int, char**)
{
    ConsoleLogger consoleLogger;
    juce::Logger::setCurrentLogger (&consoleLogger);

    juce::UnitTestRunner runner;
    runner.runAllTests();

    const int numResults = runner.getNumResults();
    if (numResults == 0)
    {
        std::cout << "ERROR: no tests were run (0 UnitTest results) - this should never report success" << std::endl;
        juce::Logger::setCurrentLogger (nullptr);
        return 1;
    }

    int failed = 0;
    for (int i = 0; i < numResults; ++i)
        if (runner.getResult (i)->failures > 0)
            ++failed;

    juce::Logger::setCurrentLogger (nullptr);
    return failed > 0 ? 1 : 0;
}
