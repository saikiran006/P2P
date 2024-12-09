#include "utils.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include<unordered_map>
#include<set>
#include <fcntl.h>
using namespace std;
bool connectToTracker(const string& ip, int port);
void updateMessage(string &message);
void handleTrackerResponse(string message, string response);
void downloadChunk(int chunkNumber, unordered_set<string> owners, const string& fileName, int offset, int blockSize, const string& destPath);
void downloadFile(string fileName,int fileSize, string destPath, map<int, unordered_set<string>> chunkDetails, int blocksize);
int createDownloadSocket();
void handleDownloadRequests(int downloadSocket);

unordered_set<string> connectedPeers;
string clientPort="None";
string downloadPort = "None";

mutex peersMutex;
#define chunkSize 336451
string calculateSizeAndNumberOfChunks(const string& filePath) {
    ifstream file(filePath, ios::binary | ios::ate);
    if (!file) {
        cerr << "Error: Could not open file at " << filePath << endl;
        return "-1";
    }

    size_t fileSize = file.tellg(); // Use size_t to avoid overflow for large files
    file.close();  // Close the file after getting its size

    // Calculate number of chunks
    size_t numChunks = fileSize / chunkSize;  // Whole chunks
    if (fileSize % chunkSize != 0) {
        numChunks++;  // Add one more chunk for the remaining bytes
    }
    cout<<"calculated number of chunks"<<numChunks<<endl;
    return to_string(fileSize) + " " + to_string(numChunks);
}
void updateMessage(string &message){
    vector<string> tokens=splitMessage(message);
    string cmd=tokens[0];
    if(cmd=="login"){
        message+=(" "+downloadPort);
    }
    else if(cmd=="upload_file"){
        string sizeAndChunks=calculateSizeAndNumberOfChunks(tokens[1]);
        cout<<sizeAndChunks<<endl;
        message+=(" "+sizeAndChunks);
    }
}

// Function to handle tracker response for download_file
void handleTrackerResponse(string message, string response) {
    if (response.substr(0, 14) == "DOWNLOAD_ERROR") {
        cout << "Error from tracker: " << response << endl;
        return; // Handle error appropriately
    }

    vector<string> messagetokens = splitMessage(message);
    if(messagetokens[0]=="download_file"){

        // Split the response into parts
        string fileName=messagetokens[2];
        string destPath=messagetokens[3];
        // Data structures to hold the parsed data
        map<int, unordered_set<string>> chunkDetails;
        vector<string> tokens = splitMessageByDelimiter(response,';');
        cout<<"tokens: ";
        for(string token : tokens) cout<<token<<endl;
        int fileSize = stoi(tokens[0]);
        // Extract chunk details
        for (size_t i = 1; i < tokens.size(); ++i) {
            string chunkDetail = tokens[i]; // This should be in the form "X:<owner1>,<owner2>;..."
            
            // Split into chunk number and owners
            size_t chunkPos = chunkDetail.find(":");
            if (chunkPos != string::npos) {
                int chunkNumber = stoi(chunkDetail.substr(0, chunkPos));
                string ownersStr = chunkDetail.substr(chunkPos + 1);

                // Split the owners by comma
                stringstream ownerStream(ownersStr);
                string owner;
                while (getline(ownerStream, owner, ',')) {
                    chunkDetails[chunkNumber].insert(owner); // Add owner to the set
                }
            }
        }

        // Output the parsed data for verification
        cout << "File Size: " << fileSize << endl;
        cout << "Chunk Details:" << endl;
        for (const auto& [chunkNumber, owners] : chunkDetails) {
            cout << "Chunk " << chunkNumber << ": ";
            for (const auto& owner : owners) {
                cout << owner << " ";
            }
            cout << endl;
        }
        downloadFile(fileName,fileSize,destPath,chunkDetails,chunkSize);
    }
}

void downloadChunk(int chunkNumber, unordered_set<string> owners, const string& fileName, int offset, int blockSize, const string& destPath) {
    cout << "In downloadChunk" << endl;
    bool downloaded = false;

    while (!downloaded) {
        for (string owner : owners) {
            int peerSocket;
            struct sockaddr_in peerAddr;

            peerAddr.sin_family = AF_INET;
            
            // Assuming owner holds IP:Port or just the port number, adjust this as needed.
            peerAddr.sin_port = htons(stoi(owner));  // Use peer's port
            
            // Peer address setup (this assumes localhost or specific IP, adjust for real IP)
            peerAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // or use inet_pton for actual IP

            peerSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (peerSocket < 0) {
                cerr << "Error: Failed to create socket for chunk " << chunkNumber << " from " << owner << endl;
                continue;  // Try other peers
            }

            if (connect(peerSocket, (struct sockaddr*)&peerAddr, sizeof(peerAddr)) < 0) {
                cerr << "Error: Failed to connect to peer " << owner << " for chunk " << chunkNumber << endl;
                close(peerSocket);
                continue;  // Try other peers
            }

            // Send request to download the chunk
            string request = "DOWNLOAD_CHUNK " + to_string(chunkNumber) + " " + fileName + "\n";
            send(peerSocket, request.c_str(), request.size(), 0);

            // Buffer to store received chunk
            char buffer[blockSize];
            ssize_t bytesReceived = recv(peerSocket, buffer, blockSize, 0);

            if (bytesReceived <= 0) {
                cerr << "Error: Failed to receive chunk " << chunkNumber << " from " << owner << endl;
                close(peerSocket);
                continue;  // Try other peers
            }

            // Write the chunk to the file at the correct offset
            string outputFilePath = destPath + "/" + fileName;
            int file = open(outputFilePath.c_str(), O_RDWR | O_CREAT, 0600);  // Ensure file is created if not exists
            if (file < 0) {
                cerr << "Unable to open file." << endl;
                close(peerSocket);  // Ensure socket is closed
                return;
            }

            if (lseek(file, offset, SEEK_SET) < 0) {
                cerr << "Unable to seek to the correct offset." << endl;
                close(file);  // Close file in case of error
                close(peerSocket);  // Ensure socket is closed
                return;
            }

            // Write the chunk to the file at the correct offset
            ssize_t bytesWritten = write(file, buffer, bytesReceived);
            if (bytesWritten < 0) {
                cerr << "Failed to write chunk to file." << endl;
            }

            // Close the file descriptor after writing
            close(file);

            cout << "Downloaded chunk " << chunkNumber << " from " << owner << " at offset " << offset << endl;

            // Clean up
            close(peerSocket);
            {
                lock_guard<mutex> lock(peersMutex);
                connectedPeers.erase(owner);
            }

            downloaded = true;
            break;  // Exit loop after successful download
        }
    }
}



void downloadFile(string fileName, int fileSize, string destPath, map<int, unordered_set<string>> chunkDetails, int blocksize) {
    cout << "In downloadFile" << endl;

    // Ensure the destination directory exists
    createDirectory(destPath);
    string outputFilePath = destPath + "/" + fileName;

    // Open the output file for writing (overwrite mode)
    ofstream outputFile(outputFilePath, ios::binary | ios::trunc);  // Overwrite the file if it exists
    if (!outputFile) {
        cerr << "Error: Failed to open output file for writing: " << outputFilePath << endl;
        return;
    }

    // Pre-allocate the file size by seeking to the end and writing a dummy byte
    outputFile.seekp(fileSize - 1);
    outputFile.write("", 1);  // Write a single byte to ensure the file has the correct size
    outputFile.close();  // Close after pre-allocating

    // Now start downloading chunks using threads
    vector<thread> downloadThreads;
    std::set<std::pair<int, int>, CompareOwnersCounts> chunkOwnersCount;

    for (const auto& [chunkNumber, owners] : chunkDetails) {
        chunkOwnersCount.insert({chunkNumber, owners.size()});
    }

    for (const auto& [chunkNumber, ownersCount] : chunkOwnersCount) {
        int offset = (chunkNumber - 1) * chunkSize;
        const auto& owners = chunkDetails[chunkNumber];

        // Start a thread for each chunk download
        downloadThreads.emplace_back(downloadChunk, chunkNumber, owners, fileName, offset, blocksize, destPath);
    }

    // Join all threads
    for (auto& thread : downloadThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    cout << "All chunks downloaded to " << outputFilePath << "." << endl;
}



// Function to create a new socket for listening to download requests
int createDownloadSocket() {
    int downloadSocket;
    struct sockaddr_in downloadAddr;

    // Create a new socket
    downloadSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (downloadSocket == -1) {
        cerr << "Error: Failed to create download socket" << endl;
        return -1;
    }

    // Set up the address structure
    downloadAddr.sin_family = AF_INET;
    downloadAddr.sin_addr.s_addr = INADDR_ANY; // Bind to any available interface
    downloadAddr.sin_port = htons(0); // Let the OS choose a free port

    // Bind the socket
    if (bind(downloadSocket, (struct sockaddr*)&downloadAddr, sizeof(downloadAddr)) < 0) {
        cerr << "Error: Failed to bind download socket" << endl;
        close(downloadSocket);
        return -1;
    }

    // Get the actual port number assigned by the OS
    socklen_t addrLen = sizeof(downloadAddr);
    if (getsockname(downloadSocket, (struct sockaddr*)&downloadAddr, &addrLen) == -1) {
        cerr << "Error: Could not get local socket information" << endl;
        close(downloadSocket);
        return -1;
    }
    downloadPort = to_string(ntohs(downloadAddr.sin_port)); // Get assigned port
    cout << "Listening for download requests on port: " << downloadPort << endl;

    // Start listening for incoming connections
    if (listen(downloadSocket, 5) < 0) {
        cerr << "Error: Failed to listen on download socket" << endl;
        close(downloadSocket);
        return -1;
    }

    return downloadSocket;
}

void sendChunkToPeer(int peerSocket, int chunkNumber, const string& fileName) {
    char chunk[chunkSize];

    // Calculate the offset for the specific chunk (chunkNumber starts from 1)
    size_t offset = (chunkNumber - 1) * chunkSize;

    // Open the original file for reading
    ifstream file(fileName, ios::binary);
    if (!file) {
        cerr << "Error: File " << fileName << " not found" << endl;
        return; // If the file doesn't exist, exit the function
    }

    // Seek to the calculated offset
    file.seekg(offset);
    if (file.fail()) {
        cerr << "Error: Failed to seek to offset " << offset << " in file " << fileName << endl;
        return;
    }

    // Read the chunk data from the file
    file.read(chunk, chunkSize);
    size_t bytesRead = file.gcount(); // Get the number of bytes actually read

    // Send the chunk data to the peer
    ssize_t bytesSent = send(peerSocket, chunk, bytesRead, 0);
    if (bytesSent < 0) {
        cerr << "Error: Failed to send chunk to peer" << endl;
    } else {
        cout << "Sent " << bytesRead << " bytes of chunk " << chunkNumber << " to peer" << endl;
    }

    file.close(); // Close the file after reading
}

// Function to handle incoming download requests
void handleDownloadRequests(int downloadSocket) {
    struct sockaddr_in peerAddr;
    socklen_t addrLen = sizeof(peerAddr);
    char buffer[1024];

    while (true) {
        int peerSocket = accept(downloadSocket, (struct sockaddr*)&peerAddr, &addrLen);
        if (peerSocket < 0) {
            cerr << "Error: Failed to accept connection" << endl;
            continue; // Continue to accept other connections
        }

        cout << "Accepted connection from: " << inet_ntoa(peerAddr.sin_addr) << endl;

        // Receive download request from the peer
        ssize_t bytesReceived = recv(peerSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; // Null-terminate the received string
            cout << "Received download request: " << buffer << endl;
            char command[20], fileName[100];
            int chunkNumber;
            if (sscanf(buffer, "%s %d %s", command, &chunkNumber, fileName) == 3) {
                if (strcmp(command, "DOWNLOAD_CHUNK") == 0) {
                    // Handle the chunk download request
                    cout << "Requesting chunk " << chunkNumber << " of file " << fileName << endl;

                    // Send the requested chunk to the peer
                    sendChunkToPeer(peerSocket, chunkNumber, fileName);

                } else {
                    cerr << "Error: Unknown command received" << endl;
                }
            } else {
                cerr << "Error: Invalid download request format" << endl;
            }
        }

        close(peerSocket); // Close the peer socket after handling the request
    }
}

bool connectToTracker(const string& ip, int port) {
    int clientSocket;
    struct sockaddr_in trackerAddr, localAddr;
    socklen_t addrLen = sizeof(localAddr);

    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        cerr << "Error: Failed to create socket" << endl;
        return false;
    }

    trackerAddr.sin_family = AF_INET;
    trackerAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    trackerAddr.sin_port = htons(port);

    // Attempt to connect to the tracker
    if (connect(clientSocket, (struct sockaddr*)&trackerAddr, sizeof(trackerAddr)) < 0) {
        cerr << "Error: Connection to " << ip << ":" << port << " failed" << endl;
        close(clientSocket);
        return false;
    }

    cout << "Connected to tracker at " << ip << ":" << port << endl;

    if (getsockname(clientSocket, (struct sockaddr*)&localAddr, &addrLen) == 0) {
        clientPort = to_string(ntohs(localAddr.sin_port));
        cout << "Client is using local port: " << clientPort << endl;
    } else {
        cerr << "Error: Could not get local socket information" << endl;
    }

    // Create a new socket for download requests
    int downloadSocket = createDownloadSocket();
    if (downloadSocket == -1) {
        return false; // Failed to create download socket
    }

    thread downloadThread(handleDownloadRequests, downloadSocket);
    downloadThread.detach();

    string message;
    while (true) {
        cout << "Enter message to send (type 'exit' to quit): ";
        getline(cin, message);

        if (message == "exit") {
            break; // Exit the loop if the user types "exit"
        }
        updateMessage(message);
        // Send the message to the tracker
        ssize_t bytesSent = send(clientSocket, message.c_str(), message.size(), 0);
        if (bytesSent < 0) {
            cerr << "Error: Failed to send message to tracker" << endl;
            break; // Exit the loop on failure
        }

        cout << "Sent message to tracker: " << message << endl;

        // Optionally, you can receive a response from the tracker
        char buffer[1024];
        ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; // Null-terminate the received string
            cout << "Received response from tracker: " << buffer << endl;
            handleTrackerResponse(message, buffer);
        } else if (bytesReceived < 0) {
            cerr << "Error: Failed to receive response from tracker" << endl;
            break; // Exit the loop on failure
        }
    }

    // Close the client socket when done
    close(clientSocket);
    close(downloadSocket);
    cout << "Connection closed." << endl;

    return true;
}



int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <tracker_file>" << endl;
        return -1;
    }

    string fileName = argv[1];
    string ip;
    int port;

    // Attempt to connect to the first tracker
    if (readIPAndPort(fileName, 1, ip, port)) {
        if (connectToTracker(ip, port)) {
            return 0; // Successfully connected to the first tracker
        }
    }

    // If the first tracker is down, attempt to connect to the second tracker
    if (readIPAndPort(fileName, 2, ip, port)) {
        if (connectToTracker(ip, port)) {
            return 0; // Successfully connected to the second tracker
        }
    }

    cerr << "Error: Unable to connect to both trackers." << endl;
    return -1;
}