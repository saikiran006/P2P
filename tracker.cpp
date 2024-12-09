#include "utils.h"
#include<unordered_map>
#include<unordered_set>
#include<map>
#include <mutex>

thread_local string currentUser = "None";

unordered_map<string,string> userCreds;
unordered_map<string,string> groups;
unordered_map<string,string> userPorts;
unordered_set<string> loggedInUsers;
unordered_map<string,unordered_set<string>> groupMembers;
unordered_map<string,unordered_set<string>> groupRequests;
unordered_map<string,unordered_set<string>> groupFiles;

//groupId+filePath -> {chunkNUmber->set(owners)}
unordered_map<string,map<int,unordered_set<string>>> fileOwners;
//groupId+filePath -> fileSize;
unordered_map<string,int> fileSizeMap;

// groupId+usersId->filePath
unordered_map<string,unordered_set<string>> userFiles;

mutex mtx;

// Command: Create User
string createUser(const vector<string>& tokens) {
    if (tokens.size() < 3) return "Usage: create_user <username> <password>";

    string user = tokens[1], password = tokens[2];
    lock_guard<mutex> lock(mtx);
    if (userCreds.find(user) != userCreds.end()) return "Error: User already exists";

    userCreds[user] = password;
    return "User Created Successfully";
}

// Command: Login
string login(const vector<string>& tokens) {
    if (tokens.size() < 4) return "Usage: login <username> <password>";

    string user = tokens[1], password = tokens[2], port = tokens[3];
    lock_guard<mutex> lock(mtx);
    if (userCreds.find(user) == userCreds.end()) return "Error: User does not exist";
    if (userCreds[user] != password) return "Error: Invalid password";

    currentUser = user;
    loggedInUsers.insert(user);
    userPorts[user] = port;

    return "Login Successful";
}

// Command: Logout
string logout() {
    if (currentUser == "None") return "Error: No user is logged in";
    
    lock_guard<mutex> lock(mtx);
    userPorts.erase(currentUser);
    loggedInUsers.erase(currentUser);
    currentUser = "None";

    return "Logout Successful";
}

// Command: List Groups
string listGroups() {
    lock_guard<mutex> lock(mtx);
    string groupsList = "List of groups: ";
    for (const auto& it : groups) groupsList += it.first + " ";
    return groupsList;
}

// Command: Create Group
string createGroup(const vector<string>& tokens) {
    if (tokens.size() < 2) return "Usage: create_group <groupId>";

    string groupId = tokens[1];
    lock_guard<mutex> lock(mtx);
    if (groups.find(groupId) != groups.end()) return "Error: Group already exists";

    groups[groupId] = currentUser;
    groupMembers[groupId].insert(currentUser);
    return "Group created";
}

// Command: Join Group
string joinGroup(const vector<string>& tokens) {
    if (tokens.size() < 2) return "Usage: join_group <groupId>";

    string groupId = tokens[1];
    lock_guard<mutex> lock(mtx);
    if (groups.find(groupId) == groups.end()) return "Error: Group not found";

    unordered_set<string>& groupUsers = groupMembers[groupId];
    if (groupUsers.find(currentUser) != groupUsers.end()) return "Error: User is already in the group";

    groupRequests[groupId].insert(currentUser);
    return "Group join request created";
}

// Command: List Requests
string listRequests(const vector<string>& tokens) {
    if (tokens.size() < 2) return "Usage: list_requests <groupId>";

    string groupId = tokens[1];
    lock_guard<mutex> lock(mtx);
    if (groups.find(groupId) == groups.end()) return "Error: Group not found";
    if (groups[groupId] != currentUser) return "Error: Only group owner can view requests";

    string requestList = "Pending requests: ";
    for (const string& user : groupRequests[groupId]) requestList += user + " ";
    return requestList;
}

// Command: Accept Request
string acceptRequest(const vector<string>& tokens) {
    if (tokens.size() < 3) return "Usage: accept_request <groupId> <user>";

    string groupId = tokens[1], user = tokens[2];
    lock_guard<mutex> lock(mtx);
    if (groups.find(groupId) == groups.end()) return "Error: Group not found";
    if (groups[groupId] != currentUser) return "Error: Only group owner can accept requests";

    unordered_set<string>& requests = groupRequests[groupId];
    if (requests.find(user) == requests.end()) return "Error: No request found for this user";

    unordered_set<string>& members = groupMembers[groupId];
    members.insert(user);
    requests.erase(user);
    return "User added successfully";
}

// Command: Upload File
string uploadFile(const vector<string>& tokens) {
    if (tokens.size() < 5) return "Usage: upload_file <filePath> <groupId> <numChunks>";

    string filePath = tokens[1], groupId = tokens[2];
    int fileSize = stoi(tokens[3]);
    int numChunks = stoi(tokens[4]);

    lock_guard<mutex> lock(mtx);
    if (groups.find(groupId) == groups.end()) return "Error: Group not found";

    groupFiles[groupId].insert(filePath);
    userFiles[groupId + currentUser].insert(filePath);

    map<int, unordered_set<string>> chunkOwners;
    for (int i = 1; i <= numChunks; i++) {
        chunkOwners[i].insert(currentUser);
    }
    fileSizeMap[groupId+filePath]=fileSize;
    fileOwners[groupId + filePath] = chunkOwners;

    return "File uploaded successfully";
}

// Command: List Files
string listFiles(const vector<string>& tokens) {
    if (tokens.size() < 2) return "Usage: list_files <groupId>";

    string groupId = tokens[1];
    lock_guard<mutex> lock(mtx);
    if (groupFiles.find(groupId) == groupFiles.end()) return "Error: Group not found";

    string message = "Files in the group: ";
    for (const string& file : groupFiles[groupId]) {
        message += file + " ";
    }
    return message;
}

// Command: Leave Group
string leaveGroup(const vector<string>& tokens) {
    if (tokens.size() < 2) return "Usage: leave_group <groupId>";

    string groupId = tokens[1];
    lock_guard<mutex> lock(mtx);

    // Check if the user is part of the group
    if (groupMembers[groupId].find(currentUser) == groupMembers[groupId].end()) {
        return "Error: User is not part of this group";
    }

    // Handle file ownership before leaving the group
    unordered_set<string>& files = userFiles[groupId + currentUser];
    for (const string& filePath : files) {
        map<int, unordered_set<string>>& fileChunksMap = fileOwners[groupId + filePath];
        int missingChunks = 0;

        for (auto& [chunkNumber, owners] : fileChunksMap) {
            if (owners.find(currentUser) != owners.end()) {
                owners.erase(currentUser);
                if (owners.empty()) missingChunks++;
            }
        }

        if (missingChunks > 0) {
            groupFiles[groupId].erase(filePath);
            fileOwners.erase(groupId + filePath);
        }
    }

    userFiles.erase(groupId + currentUser);
    groupMembers[groupId].erase(currentUser);

    // Check if the current user is the group owner
    if (groups[groupId] == currentUser) {
        if (!groupMembers[groupId].empty()) {
            // Assign ownership to another member if there are remaining members
            groups[groupId] = *groupMembers[groupId].begin();
        } else {
            // No remaining members, delete the group
            groups.erase(groupId);
            groupMembers.erase(groupId);
            groupFiles.erase(groupId);
            groupRequests.erase(groupId);
            return "User left and group has been deleted (no remaining members)";
        }
    }

    return "User left the group";
}

string downloadFile(const vector<string>& tokens) {
    if (tokens.size() < 3) 
        return "DOWNLOAD_ERROR: usage: download_file <groupId> <fileName>";
    
    string grpId = tokens[1], fileName = tokens[2];
    
    // Check if group exists
    if (groups.find(grpId) == groups.end()) 
        return "DOWNLOAD_ERROR: Group does not exist";
    
    // Check if the file exists in the group
    if (groupFiles[grpId].find(fileName) == groupFiles[grpId].end()) 
        return "DOWNLOAD_ERROR: File does not exist in the group";
    
    ostringstream response;
    int fileSize = fileSizeMap[grpId + fileName];
    response<<fileSize<<";";
    // Fetch chunk map for the file
    const auto& chunkMap = fileOwners[grpId + fileName];

    for (const auto& [chunkNumber, owners] : chunkMap) {
        vector<string> onlineOwners;

        // Iterate over the owners and check if they are logged in
        for (const string& user : owners) {
            if (loggedInUsers.find(user) != loggedInUsers.end()) {
                onlineOwners.push_back(userPorts[user]);
            }
        }

        // If no owner for this chunk is online, return an error with specific chunk number
        if (onlineOwners.empty()) 
            return "DOWNLOAD_ERROR: No online seeders for chunk " + to_string(chunkNumber);
        
        // Append the chunk info to the response
        response << chunkNumber << ":" << join(onlineOwners, ",") << ";";  // Assuming you have a join function
    }

    return response.str();
}

// Main message handler
string handleMessage(const string& message) {
    vector<string> tokens = splitMessage(message);
    if (tokens.empty()) return "Error: Empty command";

    string cmd = tokens[0];
    if((cmd != "create_user" && cmd != "login") && currentUser == "None") 
    return "please login";
    else if (cmd == "create_user") return createUser(tokens);
    else if (cmd == "login") return login(tokens);
    else if (cmd == "logout") return logout();
    else if (cmd == "list_groups") return listGroups();
    else if (cmd == "create_group") return createGroup(tokens);
    else if (cmd == "join_group") return joinGroup(tokens);
    else if (cmd == "list_requests") return listRequests(tokens);
    else if (cmd == "accept_request") return acceptRequest(tokens);
    else if (cmd == "upload_file") return uploadFile(tokens);
    else if (cmd== "download_file") return downloadFile(tokens);
    else if (cmd == "list_files") return listFiles(tokens);
    else if (cmd == "leave_group") return leaveGroup(tokens);
    return "Error: Invalid command";
}
// Function to handle client connections
void handleClient(int clientSocket) {
    cout << "Client connected. Handling client in a separate thread." << endl;

    string message;
    char buffer[1024];

    while (true) {
        ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            cerr << "Error: Client disconnected or failed to receive message." << endl;
            break; // Exit the loop if the client disconnects or an error occurs
        }

        buffer[bytesReceived] = '\0';  // Null-terminate the received string
        message = string(buffer);
        // cout << "Received message from client: " << message << endl;

        if (message == "exit") {
            cout << "Client requested to disconnect." << endl;
            break; // Exit the loop if the client types "exit"
        }
        string response=handleMessage(message);
        send(clientSocket, response.c_str(), response.size(), 0);
    }

    close(clientSocket);
    cout << "Client disconnected." << endl;
}

// Function to start the tracker
void startTracker(const string& ip, int port) {
    int trackerSocket;
    struct sockaddr_in trackerAddr;

    trackerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (trackerSocket == -1) {
        cerr << "Failed to create socket" << endl;
        return;
    }

    trackerAddr.sin_family = AF_INET;
    trackerAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    trackerAddr.sin_port = htons(port);

    if (bind(trackerSocket, (struct sockaddr*)&trackerAddr, sizeof(trackerAddr)) < 0) {
        cerr << "Bind failed for " << ip << ":" << port << endl;
        close(trackerSocket);
        return;
    }

    if (listen(trackerSocket, 1) < 0) {
        cerr << "Listen failed for " << ip << ":" << port << endl;
        close(trackerSocket);
        return;
    }

    cout << "Tracker started at " << ip << ":" << port << endl;

    while (true) {

        int clientSocket;
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        clientSocket = accept(trackerSocket, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket < 0) {
            cerr << "Accept failed" << endl;
            continue;
        }

        thread clientThread(handleClient, clientSocket);
        clientThread.detach();  // Detach the thread to handle the client
    }

    close(trackerSocket);
    cout << "Tracker socket closed." << endl;
}


int main(int argc, char **argv) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <filename> <tracker_number (1 or 2)>" << endl;
        return -1;
    }

    string fileName = argv[1];
    int trackerNumber = stoi(argv[2]);

    if (trackerNumber < 1 || trackerNumber > 2) {
        cerr << "Error: tracker_number must be 1 or 2" << endl;
        return -1;
    }

    string ip;
    int port;

    if (!readIPAndPort(fileName, trackerNumber, ip, port)) {
        return -1;  
    }

    cout << ip << " " << port << endl;
    createTrackerChangesFile();
    // Start the tracker
    startTracker(ip, port);

    cout << "Quitting tracker..." << endl;

    // Wait for input thread to set shouldQuit

    return 0;
}