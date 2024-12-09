#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fmt/color.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "websocket/websocket_client.h"
#include "api/api.h"
#include "utils/utils.h"
#include "authentication/password.h"

using namespace std;

int main() {
    // Flag to control the main loop
    bool done = false;
    char* input;
    
    // Create a websocket endpoint instance
    websocket_endpoint endpoint;

    utils::printHeader();
              
    // Main command processing loop
    while (!done) {
        // Readline provides a prompt and stores the history
        input = readline(fmt::format(fg(fmt::color::green), "deribit> ").c_str());
        if (!input) {
            break; // Exit on EOF (Ctrl+D)
        }

        string command(input);
        free(input); // Clean up the memory allocated by readline

        // Skip empty input but store valid commands in history
        if (command.empty()) {
            continue;
        }
        add_history(command.c_str());

        // Command processing
        if (command == "quit" || command == "exit") {
            done = true;
        } 
        else if (command == "help" || command == "man") {
            utils::printHelp();
        }
        else if (command.substr(0, 7) == "connect") {
            // Check if URI is provided
            if (command.length() <= 8) {
                fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, 
                           "Error: Missing URI. Usage: connect <URI>\n");
            } else {
                std::string uri = command.substr(8);
                int id = endpoint.connect(uri);
        
                if (id != -1) {
                    fmt::print(fg(fmt::color::green) | fmt::emphasis::bold, 
                               "> Successfully created connection.\n");
                    fmt::print(fg(fmt::color::cyan), "> Connection ID: {}\n", id);
                    fmt::print(fg(fmt::color::yellow), "> Status: {}\n", endpoint.get_metadata(id)->get_status());
                } else {
                    fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, 
                               "Error: Failed to create connection to {}\n", uri);
                }
            }
        }
        else if (command.substr(0, 13) == "show_messages") {
            // Show messages for a specific connection
            int id = atoi(command.substr(14).c_str());
 
            connection_metadata::ptr metadata = endpoint.get_metadata(id);
            for (const auto& msg : metadata->m_messages) {
                cout << msg << "\n\n";
            }
        }
        else if (command.substr(0, 4) == "show") {
            // Show metadata for a specific connection
            int id = atoi(command.substr(5).c_str());
 
            connection_metadata::ptr metadata = endpoint.get_metadata(id);
            if (metadata) {
                cout << *metadata << endl;
            } else {
                cout << "> Unknown connection id " << id << endl;
            }
        }
        else if (command.substr(0, 5) == "close") {
            // Close a specific connection
            stringstream ss(command);

            string cmd;
            int id;
            int close_code = websocketpp::close::status::normal;
            string reason;

            ss >> cmd >> id >> close_code;
            getline(ss, reason);

            endpoint.close(id, close_code, reason);
        }
        else if (command.substr(0, 4) == "send") {
            // Send a message on a specific connection
            stringstream ss(command);
                
            string cmd;
            int id;
            string message = "";
            
            ss >> cmd >> id;
            getline(ss, message);
            
            endpoint.send(id, message);

            // Wait for message processing
            unique_lock<mutex> lock(endpoint.get_metadata(id)->mtx);
            endpoint.get_metadata(id)->cv.wait(lock, [&] { return endpoint.get_metadata(id)->MSG_PROCESSED; });
            endpoint.get_metadata(id)->MSG_PROCESSED = false;
        }
        else if (command == "Deribit connect") {
            // Special Deribit connection
            const std::string uri = "wss://test.deribit.com/ws/api/v2";
            int id = endpoint.connect(uri);

            if (id != -1) {
                fmt::print(fg(fmt::color::green) | fmt::emphasis::bold, 
                           "> Successfully created connection to Deribit TESTNET.\n");
                fmt::print(fg(fmt::color::cyan), "> Connection ID: {}\n", id);
                fmt::print(fg(fmt::color::yellow), "> Status: {}\n", endpoint.get_metadata(id)->get_status());
            } else {
                fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, 
                           "> Failed to create connection to Deribit TESTNET.\n");
            }
        }
        else if (command.substr(0, 7) == "Deribit") {
            // Process Deribit-specific API commands
            int id; 
            string cmd;
        
            stringstream ss(command);
            ss >> cmd >> id;
            
            string msg = api::process(command);
            if (msg != "") {
                int success = endpoint.send(id, msg);
                if (success >= 0) {
                    // If it's a subscribe command, enter streaming mode
                    if (command.find("subscribe") != string::npos) {
                        fmt::print(fg(fmt::color::green), "> Streaming real-time data. Press 'q' to stop.\n");
                        
                        // Streaming loop
                        while (true) {
                            // Wait for a message with a timeout
                            unique_lock<mutex> lock(endpoint.get_metadata(id)->mtx);
                            bool data_received = endpoint.get_metadata(id)->cv.wait_for(
                                lock, 
                                chrono::seconds(5), 
                                [&] { return endpoint.get_metadata(id)->MSG_PROCESSED; }
                            );
        
                            // Check if user wants to quit
                            input = readline("");
                            if (input && (strcmp(input, "q") == 0 || strcmp(input, "Q") == 0)) {
                                free(input);
                                break;
                            }
                            
                            // Process received messages
                            if (data_received) {
                                for (const auto& msg : endpoint.get_metadata(id)->m_messages) {
                                    try {
                                        json parsed_msg = json::parse(msg);
                                        
                                        // Check if it's a subscription result or data
                                        if (parsed_msg.contains("result")) {
                                            fmt::print(fg(fmt::color::yellow), "Subscription Confirmed: {}\n", 
                                                       parsed_msg["result"].dump(2));
                                        } else if (parsed_msg.contains("params") && 
                                                   parsed_msg["params"].contains("data")) {
                                            fmt::print(fg(fmt::color::green), "Received Data: {}\n", 
                                                       parsed_msg["params"]["data"].dump(2));
                                        }
                                    } catch (const json::parse_error& e) {
                                        fmt::print(fg(fmt::color::red), "Error parsing message: {}\n", e.what());
                                    }
                                }
                                
                                // Clear processed messages
                                endpoint.get_metadata(id)->m_messages.clear();
                                endpoint.get_metadata(id)->MSG_PROCESSED = false;
                            }
                        }
                        
                        fmt::print(fg(fmt::color::yellow), "> Subscription stream stopped.\n");
                    } else {
                        // For non-subscribe commands, wait for message processing as before
                        unique_lock<mutex> lock(endpoint.get_metadata(id)->mtx);
                        endpoint.get_metadata(id)->cv.wait(lock, [&] { return endpoint.get_metadata(id)->MSG_PROCESSED; });
                        endpoint.get_metadata(id)->MSG_PROCESSED = false;
                    }
                }
            }
        }
        else {
            cout << "Unrecognized command" << endl;
        }
    }
    return 0;
}
