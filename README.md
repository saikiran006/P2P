# Peer-to-Peer File Sharing System.

## Prerequisites
- G++ compiler
1. G++ compiler
   - **To install G++ :** `sudo apt-get install g++`

# Installation

```
1. cd client
2. g++ -o client client.cpp
3. cd ../tracker
5. g++ -o tracker tracker.cpp
6. cd ..
```

## Usage

### Tracker

1. Run Tracker:

```
cd tracker
./tracker​ <TRACKER INFO FILE> <TRACKER NUMBER>
ex: ./tracker tracker_info.txt 1
```

`<TRACKER INFO FILE>` contains the IP, Port details of all the trackers.

```
Ex:
127.0.0.1
9999
127.0.0.1
8889
```

## Client:

1. Run Client:

```
cd client
./client​ <IP>:<PORT> <TRACKER INFO FILE>
ex: ./client 127.0.0.1:18000 tracker_info.txt
```

2. Create user account:

```
create_user​ <user_id> <password>
```

3. Login:

```
login​ <user_id> <password>
```

4. Create Group:

```
create_group​ <group_id>
```

5. Join Group:

```
join_group​ <group_id>
```

6. Leave Group:

```
leave_group​ <group_id>
```

7. List pending requests:

```
list_requests ​<group_id>
```

8. Accept Group Joining Request:

```
accept_request​ <group_id> <user_id>
```

9. List All Group In Network:

```
list_groups
```

10. List All sharable Files In Group:

```
list_files​ <group_id>
```

11. Upload File:

```
​upload_file​ <file_path> <group_id​>
```

12. Download File:​

```
download_file​ <group_id> <file_name> <destination_path>
```

13. Logout:​

```
logout
```
14. Stop sharing: ​

```
stop_share ​<group_id> <file_name>
```

## Assumptions

1. Only one tracker is implemented and that tracker should always be online.
2. The peer can login from different IP addresses,.
3. Download is working from one peer to another peer only.
4. Did not implement sha1 integrity check.

