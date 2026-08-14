#include "TestSupport.h"

#include <cstdlib>
#include <iostream>

int main()
{
    TestContext context;

    runLruCacheTests(context);
    runCommandParserTests(context);
    runPersistenceTests(context);
    runExpirationTests(context);
    runConcurrencyTests(context);

    if (context.failures() == 0) {
        std::cout << "All MiniKV tests passed.\n";
        return EXIT_SUCCESS;
    }

    std::cerr << context.failures() << " test(s) failed.\n";
    return EXIT_FAILURE;
}

