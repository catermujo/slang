// main.cpp

#include "core/slang-io.h"
#include "core/slang-test-tool-util.h"
#include "core/slang-writer.h"

#include <cstdio>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "slang.h"
#include "slang/slang-internal.h"

using namespace Slang;


#ifdef _WIN32
#define MAIN slangc_main
#else
#define MAIN main
#endif

static SlangResult _compile(SlangCompileRequest* compileRequest, int argc, const char* const* argv)
{
    // Use the stderr writer directly instead of a callback.
    // This allows the API to correctly detect TTY status for -diagnostic-color auto.
    spSetWriter(
        compileRequest,
        SLANG_WRITER_CHANNEL_DIAGNOSTIC,
        StdWriters::getSingleton()->getWriter(SLANG_WRITER_CHANNEL_STD_ERROR));
    spSetCommandLineCompilerMode(compileRequest);

    SlangResult res = spProcessCommandLineArguments(compileRequest, &argv[1], argc - 1);
    if (SLANG_FAILED(res))
    {
        // TODO: print usage message
        return res;
    }

    res = SLANG_OK;

#ifndef _DEBUG
    try
#endif
    {
        // Run the compiler (this will produce any diagnostics through
        // SLANG_WRITER_TARGET_TYPE_DIAGNOSTIC).
        res = spCompile(compileRequest);
        // If the compilation failed, then get out of here...
        // Turn into an internal Result -> such that return code can be used to vary result to match
        // previous behavior
        res = SLANG_FAILED(res) ? SLANG_E_INTERNAL_FAIL : res;
    }
#ifndef _DEBUG
    catch (const Exception& e)
    {
        StdWriters::getOut().print("internal compiler error: %S\n", e.Message.toWString().begin());
        res = SLANG_FAIL;
    }
#endif

    return res;
}

static bool shouldEmbedPrelude(const char* const* argv, int argc)
{
    for (int i = 0; i < argc; i++)
    {
        if (UnownedStringSlice(argv[i]) == "-embed-prelude")
            return true;
    }
    return false;
}

SLANG_TEST_TOOL_API SlangResult innerMain(
    StdWriters* stdWriters,
    slang::IGlobalSession* sharedSession,
    int argc,
    const char* const* argv)
{
    StdWriters::setSingleton(stdWriters);

    // Assume we will used the shared session
    ComPtr<slang::IGlobalSession> session(sharedSession);

    // The sharedSession always has a pre-loaded core module, is sharedSession is not nullptr.
    // This differed test checks if the command line has an option to setup the core module.
    // If so we *don't* use the sharedSession, and create a new session without the core module just
    // for this compilation.
    if (TestToolUtil::hasDeferredCoreModule(Index(argc - 1), argv + 1))
    {
        SLANG_RETURN_ON_FAIL(
            slang_createGlobalSessionWithoutCoreModule(SLANG_API_VERSION, session.writeRef()));
    }
    else if (!session)
    {
        // Just create the global session in the regular way if there isn't one set
        SlangGlobalSessionDesc desc = {};
        desc.enableGLSL = true;
        Slang::GlobalSessionInternalDesc internalDesc = {};
#ifdef SLANG_BOOTSTRAP
        internalDesc.isBootstrap = true;
#endif
        SLANG_RETURN_ON_FAIL(
            slang_createGlobalSessionImpl(&desc, &internalDesc, session.writeRef()));
    }

    if (!shouldEmbedPrelude(argv, argc))
        TestToolUtil::setSessionDefaultPreludeFromExePath(argv[0], session);

    SlangCompileRequest* compileRequest = spCreateCompileRequest(session);
    compileRequest->addSearchPath(Path::getParentDirectory(Path::getExecutablePath()).getBuffer());
    SlangResult res = _compile(compileRequest, argc, argv);
    // Now that we are done, clean up after ourselves
    spDestroyCompileRequest(compileRequest);

    return res;
}

// DUMBAI: Frame UTF-8 arguments and captured output independently of shell quoting and Windows text
// mode.
static bool readBatchUInt(UInt32& value)
{
    unsigned char bytes[4];
    if (fread(bytes, 1, sizeof(bytes), stdin) != sizeof(bytes))
        return false;
    value = UInt32(bytes[0]) | (UInt32(bytes[1]) << 8) | (UInt32(bytes[2]) << 16) |
            (UInt32(bytes[3]) << 24);
    return true;
}

static bool writeBatchUInt(UInt32 value)
{
    unsigned char bytes[] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24)};
    return fwrite(bytes, 1, sizeof(bytes), stdout) == sizeof(bytes);
}

static bool writeBatchText(const StringBuilder& text)
{
    return writeBatchUInt(UInt32(text.getLength())) &&
           fwrite(text.getBuffer(), 1, text.getLength(), stdout) == size_t(text.getLength());
}

static SlangResult serveBatch(StdWriters* defaultWriters)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    SlangGlobalSessionDesc desc = {};
    desc.enableGLSL = true;
    ComPtr<slang::IGlobalSession> session;
    SLANG_RETURN_ON_FAIL(slang::createGlobalSession(&desc, session.writeRef()));
    if (fwrite("SLB1", 1, 4, stdout) != 4 || fflush(stdout) != 0)
        return SLANG_FAIL;

    // DUMBAI: Each request retains the CLI's isolated compile request while sharing only built-in
    // modules.
    for (;;)
    {
        UInt32 argumentCount;
        if (!readBatchUInt(argumentCount))
            return feof(stdin) ? SLANG_OK : SLANG_FAIL;
        List<String> arguments;
        for (UInt32 index = 0; index < argumentCount; ++index)
        {
            UInt32 length;
            if (!readBatchUInt(length))
                return SLANG_FAIL;
            List<char> bytes;
            bytes.setCount(Index(length) + 1);
            if (fread(bytes.getBuffer(), 1, length, stdin) != length)
                return SLANG_FAIL;
            bytes[length] = 0;
            arguments.add(String(bytes.getBuffer()));
        }
        List<const char*> pointers;
        for (const auto& argument : arguments)
            pointers.add(argument.getBuffer());

        StringBuilder output;
        StringBuilder errors;
        StringWriter outWriter(&output, WriterFlag::IsStatic);
        StringWriter errorWriter(&errors, WriterFlag::IsStatic);
        StdWriters writers;
        writers.setWriter(SLANG_WRITER_CHANNEL_STD_OUTPUT, &outWriter);
        writers.setWriter(SLANG_WRITER_CHANNEL_STD_ERROR, &errorWriter);
        writers.setWriter(SLANG_WRITER_CHANNEL_DIAGNOSTIC, &errorWriter);
        SlangResult result =
            innerMain(&writers, session, int(pointers.getCount()), pointers.getBuffer());
        StdWriters::setSingleton(defaultWriters);
        if (!writeBatchUInt(UInt32(TestToolUtil::getReturnCode(result))) ||
            !writeBatchText(output) || !writeBatchText(errors) || fflush(stdout) != 0)
            return SLANG_FAIL;
    }
}

int MAIN(int argc, char** argv)
{
    auto stdWriters = StdWriters::initDefaultSingleton();
    SlangResult res = argc == 2 && UnownedStringSlice(argv[1]) == "-batch"
                          ? serveBatch(stdWriters)
                          : innerMain(stdWriters, nullptr, argc, argv);
    slang::shutdown();
    return (int)TestToolUtil::getReturnCode(res);
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    int result = 0;

#if SLANG_IGNORE_ABORT_MSG && defined(_MSC_VER)
    // Suppress the modal abort() dialog in unattended/LLM-driven builds.
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif

    {
        // Convert the wide-character Unicode arguments to UTF-8,
        // since that is what Slang expects on the API side.

        List<String> args;
        for (int ii = 0; ii < argc; ++ii)
        {
            String arg = String::fromWString(argv[ii]);
            args.add(arg);
        }

        // argBuffers holds raw pointers into the String buffers owned by args.
        // args must outlive argBuffers.
        List<char const*> argBuffers;
        for (const auto& arg : args)
            argBuffers.add(arg.getBuffer());

        result = MAIN((int)argBuffers.getCount(), (char**)&argBuffers[0]);
    }

#ifdef _MSC_VER
    // _CrtXXX functions are functional only for debug build. The spec says,
    // "When _DEBUG isn't defined, calls to _CrtSetReportMode are removed
    // during preprocessing."
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    int memleakDetected = _CrtDumpMemoryLeaks();
    SLANG_UNUSED(memleakDetected);
    SLANG_ASSERT(!memleakDetected);
#endif

    return result;
}
#endif
