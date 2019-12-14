// Copyright (c) 2009-2013 Intel Corporation
// All rights reserved.
//
// WARRANTY DISCLAIMER
//
// THESE MATERIALS ARE PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THESE
// MATERIALS, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Intel Corporation is the author of the Materials, and requests that all
// problem reports or change requests be submitted to it directly


#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>

#include <CL/cl.h>

#include "basic.hpp"
#include "cmdparser.hpp"
#include "oclobject.hpp"
#include "multidevice.hpp"


using namespace std;


bool parse_command_line (
    int argc,
    const char** argv,
    string& platform_subname_out,
    cl_device_type& device_type_out,
    size_t& work_size_out,
    Scenario& scenario_out,
    int& instance_count_out,
    int& instance_index_out   // applicable for the system-level scenario only
)
{
    CmdParserDeviceType cmdparser(argc, argv);

    CmdOption<string> context(
        cmdparser,
        'c',
        "context",
        "",
        "Type of the multi-device scenario used: with system-level partitioning, "
            "with multiple devices and multiple contexts "
            "for each device or one shared context for all devices. For one device in the system, "
            "system = multiple = shared.",
        "shared"
    );

    CmdEnum<string> context_system(
        context,
        "system"
    );

    CmdEnum<string> context_multi(
        context,
        "multi"
    );

    CmdEnum<string> context_shared(
        context,
        "shared"
    );

    CmdOption<size_t> size(
        cmdparser,
        's',
        "size",
        "<integer>",
        "Global number of work items to be divided among all devices.",
        16*1024*1024   // 16M
    );

    CmdOption<int> instance_count(
        cmdparser,
        0,
        "instance-count",
        "<integer>",
        "Applicable for system-level scenario only. Number of application "
            "instances which will participate in system-level scenario. "
            "To identify particular instance, use --instance-index key.",
        0
    );

    CmdOption<int> instance_index(
        cmdparser,
        0,
        "instance-index",
        "<integer>",
        "Applicable for system-level scenario only. Index of instance among "
            " all participating application instances which is set by "
            "--instance-count key.",
        0
    );

    cmdparser.parse();

    if(cmdparser.help.isSet())
    {
        return false;
    }

    platform_subname_out = cmdparser.platform.getValue();
    device_type_out = parseDeviceType(cmdparser.device_type.getValue());
    work_size_out = size.getValue();

    if(context_system.isSet())
    {
        scenario_out = SCENARIO_SYSTEM_LEVEL;
    }
    else if(context_multi.isSet())
    {
        scenario_out = SCENARIO_MULTI_CONTEXT;
    }
    else if(context_shared.isSet())
    {
        scenario_out = SCENARIO_SHARED_CONTEXT;
    }

    instance_count_out = instance_count.getValue();
    instance_index_out = instance_index.getValue();

    if(scenario_out == SCENARIO_SYSTEM_LEVEL)
    {
        if(instance_count.getValue() <= 0)
        {
            throw Error(
                "Value for --instance-count command line option should be "
                "positive value. You provided: " + to_str(instance_count.getValue())
            );
        }

        if(instance_count.getValue() <= instance_index.getValue())
        {
            throw Error(
                "Value for --instance-index command line option should be "
                "less than --instance-count. You provided: " +
                to_str(instance_index.getValue())
            );
        }

        if(instance_index.getValue() < 0)
        {
            throw Error(
                "Value for --instance-index command line option should be "
                "less than --instance-count. You provided: " +
                to_str(instance_index.getValue())
            );
        }
    }

    return true;
}


int main (int argc, const char** argv)
{
    try
    {
        string platform_subname;
        cl_device_type device_type;
        Scenario scenario;
        int instance_count;
        int instance_index;
        size_t work_size;

        if(!parse_command_line(
            argc, argv,
            platform_subname,
            device_type,
            work_size,
            scenario,
            instance_count,
            instance_index
        ))
        {
            return EXIT_SUCCESS;   // exit immediately because user asked for help
        }

        cl_platform_id platform = selectPlatform(platform_subname);

        switch(scenario)
        {
            case SCENARIO_SYSTEM_LEVEL:
                cout << "Executing system-level scenario." << endl;
                system_level_scenario(platform, device_type, work_size, instance_count, instance_index);
                break;
            case SCENARIO_MULTI_CONTEXT:
                cout << "Executing multi-context scenario." << endl;
                multi_context_scenario(platform, device_type, work_size);
                break;
            case SCENARIO_SHARED_CONTEXT:
                cout << "Executing shared-context scenario." << endl;
                shared_context_scenario(platform, device_type, work_size);
                break;
        }
    }
    catch(const CmdParser::Error& error)
    {
        cerr
            << "[ ERROR ] In command line: " << error.what() << "\n"
            << "Run " << argv[0] << " -h for usage info.\n";
        return EXIT_FAILURE;
    }
    catch(const Error& error)
    {
        cerr << "[ ERROR ] Sample application specific error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    catch(const exception& error)
    {
        cerr << "[ ERROR ] " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    catch(...)
    {
        cerr << "[ ERROR ] Unknown/internal error happened.\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
