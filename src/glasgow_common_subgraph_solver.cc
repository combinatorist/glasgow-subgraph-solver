#include <gss/common_subgraph.hh>
#include <gss/configuration.hh>
#include <gss/formats/read_file_format.hh>

#include <boost/program_options.hpp>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>

#include <unistd.h>

using namespace gss;
namespace po = boost::program_options;

using std::boolalpha;
using std::cerr;
using std::cout;
using std::endl;
using std::exception;
using std::function;
using std::localtime;
using std::make_optional;
using std::make_pair;
using std::make_shared;
using std::make_unique;
using std::put_time;
using std::string;
using std::string_view;

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::operator""s;
using std::chrono::seconds;
using std::chrono::steady_clock;
using std::chrono::system_clock;

auto main(int argc, char * argv[]) -> int
{
    try {
        po::options_description display_options{"Program options"};
        display_options.add_options()                                                            //
            ("help", "Display help information")                                                 //
            ("timeout", po::value<int>(), "Abort after this many seconds")                       //
            ("decide", po::value<int>(), "Solve this decision problem")                          //
            ("count-solutions", "Count the number of solutions (--decide only)")                 //
            ("print-all-solutions", "Print out every solution, rather than one (--decide only)") //
            ("connected", "Only find connected graphs")                                          //
            ("clique", "Use the clique solver");

        po::options_description input_options{"Input file options"};
        input_options.add_options()                                                                                          //
            ("format", po::value<string>(), "Specify input file format (auto, lad, vertexlabelledlad, labelledlad, dimacs)") //
            ("first-format", po::value<string>(), "Specify input file format just for the first graph")                      //
            ("second-format", po::value<string>(), "Specify input file format just for the second graph");
        display_options.add(input_options);

        po::options_description proof_logging_options{"Proof logging options"};
        proof_logging_options.add_options()                                                                        //
            ("prove", po::value<string>(), "Write unsat proofs to this filename (suffixed with .opb and .veripb)") //
            ("proof-names", "Use 'friendly' variable names in the proof, rather than x1, x2, ...")                 //
            ("verbose-proofs", "Write lots of comments to the proof, for tracing")                                 //
            ("proof-format-2", "Use the under-development 2.0 format for proofs")                                  //
            ("recover-proof-encoding", "Recover the proof encoding, to work with verified encoders");
        display_options.add(proof_logging_options);

        po::options_description all_options{"All options"};
        all_options.add_options()("first-file", "Specify the first graph file")("second-file", "Specify the second graph file");

        all_options.add(display_options);

        po::positional_options_description positional_options;
        positional_options
            .add("first-file", 1)
            .add("second-file", 1);

        po::variables_map options_vars;
        po::store(po::command_line_parser(argc, argv)
                      .options(all_options)
                      .positional(positional_options)
                      .run(),
            options_vars);
        po::notify(options_vars);

        /* --help? Show a message, and exit. */
        if (options_vars.count("help")) {
            cout << "Usage: " << argv[0] << " [options] graph-file graph-file" << endl;
            cout << endl;
            cout << display_options << endl;
            return EXIT_SUCCESS;
        }

        /* No algorithm or no input file specified? Show a message and exit. */
        if (! options_vars.count("first-file") || ! options_vars.count("second-file")) {
            cout << "Usage: " << argv[0] << " [options] graph-file graph-file" << endl;
            return EXIT_FAILURE;
        }

        /* Figure out what our options should be. */
        CommonSubgraphParams params;

        if (options_vars.count("decide"))
            params.decide = make_optional(options_vars["decide"].as<int>());

        params.connected = options_vars.count("connected");
        params.count_solutions = options_vars.count("count-solutions") || options_vars.count("print-all-solutions");
        params.clique = options_vars.count("clique");

#if ! defined(__WIN32)
        char hostname_buf[255];
        if (0 == gethostname(hostname_buf, 255))
            cout << "hostname = " << string(hostname_buf) << endl;
#endif
        cout << "commandline =";
        for (int i = 0; i < argc; ++i)
            cout << " " << argv[i];
        cout << endl;

        auto started_at = system_clock::to_time_t(system_clock::now());
        cout << "started_at = " << put_time(localtime(&started_at), "%F %T") << endl;

        /* Read in the graphs */
        string default_format_name = options_vars.count("format") ? options_vars["format"].as<string>() : "auto";
        string first_format_name = options_vars.count("first-format") ? options_vars["first-format"].as<string>() : default_format_name;
        string second_format_name = options_vars.count("second-format") ? options_vars["second-format"].as<string>() : default_format_name;
        auto first = read_file_format(first_format_name, options_vars["first-file"].as<string>());
        auto second = read_file_format(second_format_name, options_vars["second-file"].as<string>());

        cout << "first_file = " << options_vars["first-file"].as<string>() << endl;
        cout << "second_file = " << options_vars["second-file"].as<string>() << endl;

        auto describe = [&] (const InputGraph & g) {
            if (g.directed())
                cout << " directed";
            if (g.loopy())
                cout << " loopy";
            if (g.has_vertex_labels())
                cout << " vertex_labels";
            if (g.has_edge_labels())
                cout << " edge_labels";
            cout << endl;
        };

        cout << "first_properties =";
        describe(first);
        cout << "first_vertices = " << first.size() << endl;
        cout << "first_directed_edges = " << first.number_of_directed_edges() << endl;
        cout << "second_properties =";
        describe(second);
        cout << "second_vertices = " << second.size() << endl;
        cout << "second_directed_edges = " << second.number_of_directed_edges() << endl;

        if (options_vars.count("print-all-solutions")) {
            params.enumerate_callback = [&](const VertexToVertexMapping & mapping) {
                cout << "mapping = ";
                for (auto v : mapping)
                    cout << "(" << first.vertex_name(v.first) << " -> " << second.vertex_name(v.second) << ") ";
                cout << endl;
            };
        }

        if (options_vars.count("prove")) {
            string fn = options_vars["prove"].as<string>();
            ProofOptions proof_options{
                .opb_file = fn + ".opb",
                .log_file = fn + ".veripb",
                .friendly_names = options_vars.contains("proof-names"),
                .recover_encoding = options_vars.contains("recover-proof-encoding"),
                .super_extra_verbose = options_vars.contains("verbose-proofs"),
                .version2 = options_vars.contains("proof-format-2")};
            params.proof_options = proof_options;
            cout << "proof_model = " << fn << ".opb" << endl;
            cout << "proof_log = " << fn << ".veripb" << endl;
        }

        /* Prepare and start timeout */
        params.timeout = make_shared<Timeout>(options_vars.count("timeout") ? seconds{options_vars["timeout"].as<int>()} : 0s);

        /* Start the clock */
        params.start_time = steady_clock::now();

        auto result = solve_common_subgraph_problem(first, second, params);

        /* Stop the clock. */
        auto overall_time = duration_cast<milliseconds>(steady_clock::now() - params.start_time);

        params.timeout->stop();

        cout << "status = ";
        if (params.timeout->aborted())
            cout << "aborted";
        else if ((! result.mapping.empty()) || (params.count_solutions && result.solution_count > 0))
            cout << "true";
        else
            cout << "false";
        cout << endl;

        if (params.count_solutions)
            cout << "solution_count = " << result.solution_count << endl;

        cout << "nodes = " << result.nodes << endl;

        if (! result.mapping.empty() && ! options_vars.count("print-all-solutions")) {
            cout << "size = " << result.mapping.size() << endl;
            cout << "mapping = ";
            for (auto v : result.mapping)
                cout << "(" << first.vertex_name(v.first) << " -> " << second.vertex_name(v.second) << ") ";
            cout << endl;
        }

        cout << "runtime = " << overall_time.count() << endl;

        for (const auto & s : result.extra_stats)
            cout << s << endl;

        return EXIT_SUCCESS;
    }
    catch (const GraphFileError & e) {
        cerr << "Error: " << e.what() << endl;
        if (e.file_at_least_existed())
            cerr << "Maybe try specifying --format?" << endl;
        return EXIT_FAILURE;
    }
    catch (const po::error & e) {
        cerr << "Error: " << e.what() << endl;
        cerr << "Try " << argv[0] << " --help" << endl;
        return EXIT_FAILURE;
    }
    catch (const exception & e) {
        cerr << "Error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
}
