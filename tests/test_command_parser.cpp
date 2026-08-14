#include "TestSupport.h"

#include "minikv/CommandParser.h"

#include <chrono>

void runCommandParserTests(TestContext& context)
{
    std::cout << "\n== Command parser ==\n";

    auto setResult = minikv::CommandParser::parse("SET greeting \"hello world\" TTL 60");
    context.check(setResult.ok(), "SET with quoted value and TTL parses");
    context.check(setResult.ok() && setResult.value().type == minikv::CommandType::Set,
                  "SET command type is correct");
    context.check(setResult.ok() && setResult.value().value == "hello world",
                  "quoted value preserves spaces");
    context.check(setResult.ok() && setResult.value().ttl == std::chrono::seconds(60),
                  "TTL seconds are converted to milliseconds");

    auto emptyValue = minikv::CommandParser::parse("SET empty \"\"");
    context.check(emptyValue.ok() && emptyValue.value().value.empty(),
                  "empty quoted values are supported");

    auto escaped = minikv::CommandParser::parse("SET text \"say \\\"hello\\\"\"");
    context.check(escaped.ok() && escaped.value().value == "say \"hello\"",
                  "quoted escape sequences are parsed");

    context.check(!minikv::CommandParser::parse("SET key value TTL 0").ok(),
                  "zero TTL is rejected");
    context.check(!minikv::CommandParser::parse("SET key \"unfinished").ok(),
                  "missing quote is rejected");
    context.check(!minikv::CommandParser::parse("UNKNOWN anything").ok(),
                  "unknown commands are rejected");
}

