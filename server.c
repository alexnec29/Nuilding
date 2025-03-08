#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdbool.h>

#define PORT 2909

extern int errno;

static void *treat(void *);

typedef struct
{
    pthread_t idThread;
    int thCount;
} Thread;

Thread *threadsPool;

int sd;
int nthreads;
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mlockcommand = PTHREAD_MUTEX_INITIALIZER;

sqlite3 *db;

char buildings_order[256] = "ORDER BY id";
char rooms_order[256] = "ORDER BY id";

const char *help_message = 
    "Welcome to Nuilding\n\n"
    "Building table variables:\n"
    "---id, name, address, owner, capacity, floors, description\n\n"
    "Rooms table variables:\n"
    "--id, building_name, name, room_type, owner, capacity, floor, image_url, description\n\n"
    "Commands list:\n"
    "1) add_building name:{name} [, field1:{value}, field2:{value}, (...)]\n"
    "   - Adds a building to the database with specified attributes.\n"
    "   Example:\n"
    "     add_building name:Main Building, capacity:500, owner:John Doe, description:Office Building\n\n"
    
    "2) modify_building name:{name} [, field1:{value}, field2:{value}, (...)]\n"
    "   - Modifies the attributes of an existing building.\n"
    "   Example:\n"
    "     modify_building name:Main Building, capacity:600, floors:6\n\n"
    
    "3) view_buildings [{condition1}, {condition2}, (...)]\n"
    "   - Displays all buildings that match the specified conditions, optionally ordered by a field, depending on settings.\n"
    "   Example:\n"
    "     view_buildings floors>3, capacity>500, owner=\"John Doe\"\n\n"
    
    "4) delete_building {name}\n"
    "   - Deletes the specified building and all its associated rooms.\n"
    "   Example:\n"
    "     delete_building Main Building\n\n"
    
    "5) add_room building_name:{building_name}, name:{room_name} [, field1:{value}, field2:{value}, (...)]\n"
    "   - Adds a room to a building with specified attributes.\n"
    "   Example:\n"
    "     add_room building_name:Main Building, name:Conference Room, room_type:meeting, capacity:50, floor:2, description:Spacious\n\n"
    
    "6) modify_room name:{room_name} [, field1:{value}, field2:{value}, (...)]\n"
    "   - Modifies the attributes of a specified room.\n"
    "   Example:\n"
    "     modify_room name:Conference Room, capacity:60\n\n"
    
    "7) view_rooms [{condition1}, {condition2}, (...)]\n"
    "   - Displays all rooms in a building that match the specified conditions, ordered depending on the settings.\n"
    "   Example:\n"
    "     view_rooms building_name=\"Main Building\", capacity>300\n\n"
    
    "8) delete_room {room_name}\n"
    "   - Deletes the specified room from the database.\n"
    "   Example:\n"
    "     delete_room Conference_Room\n\n"
    
    "9) order_buildings [field1 [desc], field2 [desc], (...)]\n"
    "   - Sets the order clause for the buildings based on one or more fields.\n"
    "   Example:\n"
    "     order_buildings owner, capacity desc\n\n"
    
    "10) order_rooms [field1 [desc], field2 [desc], (...)]\n"
    "    - Sets the order clause for the rooms based on one or more fields.\n"
    "    Example:\n"
    "      order_rooms building_name desc, floor\n\n"
    
    "Use these commands in the format provided to interact with the database.\n"
    "Ensure that attribute names and values are formatted correctly.\n";

void raspunde(int cl, int idThread);

int main(int argc, char *argv[])
{
    struct sockaddr_in server;
    void threadCreate(int);

    if (argc < 2)
    {
        fprintf(stderr, "Error: First argument is the number of threads...");
        exit(1);
    }
    nthreads = atoi(argv[1]);
    if (nthreads <= 0)
    {
        fprintf(stderr, "Error: Invalid number of threads...");
        exit(1);
    }
    threadsPool = calloc(sizeof(Thread), nthreads);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server]Error at socket().\n");
        return errno;
    }
    /* utilizarea optiunii SO_REUSEADDR */
    int on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&server, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server]Error at bind().\n");
        return errno;
    }

    if (listen(sd, 2) == -1)
    {
        perror("[server]Error at listen().\n");
        return errno;
    }

    printf("Number of threads: %d \n", nthreads);
    fflush(stdout);
    int i;
    for (i = 0; i < nthreads; i++)
        threadCreate(i);

    for (;;)
    {
        printf("[server]Waiting at port %d...\n", PORT);
        pause();
    }
};

void threadCreate(int i)
{
    void *treat(void *);
    pthread_create(&threadsPool[i].idThread, NULL, &treat, (void *)i);
    return;
}

void *treat(void *arg)
{
    int client;

    struct sockaddr_in from;
    bzero(&from, sizeof(from));
    printf("[thread]- %d - ON...\n", (int)arg);
    fflush(stdout);

    for (;;)
    {
        int length = sizeof(from);
        pthread_mutex_lock(&mlock);
        if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
        {
            perror("[thread]Error at accept().\n");
        }
        pthread_mutex_unlock(&mlock);
        threadsPool[(int)arg].thCount++;

        raspunde(client, (int)arg);
        close(client);
    }
}

//blocul de primire/trimitere server
void raspunde(int cl, int idThread)
{
    char buf[512];
    char raspuns[512];
    create_database();
    while(1){
        bzero(buf, 512);
        fflush(stdout);
        ssize_t bufread = read(cl, buf, 512);
        if (bufread < 0)
        {
            printf("[Thread %d]\n",idThread);
			perror("Error at read() from client.\n");
            break;
        }

        if (bufread == 0)
        {
            printf("[Thread %d]\n",idThread);
			printf("\nClient disconnected\n");
            break;
        }

        printf("[Thread %d] Command: '%s'\n", idThread, buf);
        bzero(raspuns,512);
        char *p = strtok(buf, " ");
        if (p == NULL)
        {
            strcat(raspuns, "Unknown command. Use 'help' for more info\n");
            write(cl, raspuns, strlen(raspuns));
        }
        else
        {
            pthread_mutex_lock(&mlockcommand);
            if (strcmp(p, "help") == 0)
            {
                write(cl, help_message, strlen(help_message));
            }
            else if (strcmp(p, "add_building") == 0)
            {
                add_building(cl, buf+13);
            }
            else if (strcmp(p, "delete_building") == 0)
            {
                delete_building(cl, buf+16);
            }
            else if (strcmp(p, "view_buildings") == 0)
            {
                view_buildings(cl, buf+15);
            }
            else if (strcmp(p, "modify_building") == 0)
            {
                modify_building(cl, buf+16);
            }
            else if (strcmp(p, "add_room") == 0)
            {
                add_room(cl, buf+9);
            }
            else if (strcmp(p, "delete_room") == 0)
            {
                delete_room(cl, buf+12);
            }
            else if (strcmp(p, "view_rooms") == 0)
            {
                view_rooms(cl, buf+11);
            }
            else if (strcmp(p, "modify_room") == 0)
            {
                modify_room(cl, buf+12);
            }
            else if (strcmp(p, "order_buildings") == 0)
            {
                order_buildings(cl, buf+16);
            }
            else if (strcmp(p, "order_rooms") == 0)
            {
                order_rooms(cl, buf+12);
            }
            else
            {
                strcat(raspuns, "Unknown command. Use 'help' for more info\n");
                write(cl, raspuns, strlen(raspuns));
            }
            pthread_mutex_unlock(&mlockcommand);
        }
    }
}

void create_database() 
{
    char *err_msg = NULL;
    int rc;

    rc = sqlite3_open("nuilding.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", 0, 0, &err_msg);

    const char *sql_buildings = 
        "CREATE TABLE IF NOT EXISTS buildings ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT UNIQUE NOT NULL, "
        "address TEXT, "
        "owner TEXT, "
        "capacity INTEGER, "
        "floors INTEGER, "
        "description TEXT);";

    const char *sql_rooms = 
        "CREATE TABLE IF NOT EXISTS rooms ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "building_name TEXT NOT NULL, "
        "name TEXT UNIQUE NOT NULL, "
        "room_type TEXT, "
        "owner TEXT, "
        "capacity INTEGER, "
        "floor TEXT,"
        "image_url TEXT, "
        "description TEXT, "
        "FOREIGN KEY(building_name) REFERENCES buildings(name) ON DELETE CASCADE);";

    rc = sqlite3_exec(db, sql_buildings, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error while creating 'buildings': %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_exec(db, sql_rooms, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error while creating 'rooms': %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        exit(1);
    }

    printf("Database and tables initialized successfully.\n");
}

void add_building(int client, char *input) 
{
    char *err_msg = NULL;
    bool nameAdded = false;
    bool addressAdded = false, ownerAdded=false, capacityAdded=false;
    bool floorsAdded=false, descriptionAdded=false;
    char name[256] = "", address[256] = "", owner[256] = "";
    int capacity = 0, floors = 0;
    char description[512] = "";
    char raspuns[512];

    char *p = strtok(input, ",");
    while (p != NULL) 
    {
        while (*p == ' ') p++;
        if (strncmp(p, "name:", 5) == 0 && !nameAdded) 
        {
            strncpy(name, p + 5, sizeof(name) - 1);
            nameAdded = true;
        } 
        else if (strncmp(p, "address:", 8) == 0 && !addressAdded) 
        {
            strncpy(address, p + 8, sizeof(address) - 1);
            addressAdded=true;
        } 
        else if (strncmp(p, "owner:", 6) == 0 && !ownerAdded) 
        {
            strncpy(owner, p + 6, sizeof(owner) - 1);
            ownerAdded=true;
        } 
        else if (strncmp(p, "capacity:", 9) == 0 && !capacityAdded) 
        {
            capacity = atoi(p + 9);
            capacityAdded=true;
        } 
        else if (strncmp(p, "floors:", 7) == 0 && !floorsAdded) 
        {
            floors = atoi(p + 7);
            floorsAdded=true;
        } 
        else if (strncmp(p, "description:", 12) == 0 && !descriptionAdded) 
        {
            strncpy(description, p + 12, sizeof(description) - 1);
            descriptionAdded=true;
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: Not a valid field or a repeated field: %s\n", p);
            write(client, raspuns, strlen(raspuns));
            return;
        }
        p = strtok(NULL, ",");
    }

    if (!nameAdded) 
    {
        strcpy(raspuns, "Error: Missing 'name' field, which is required\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    char sql[2048];
    bzero(sql, 2048);
    snprintf(sql, sizeof(sql),
             "INSERT INTO buildings (name, address, owner, capacity, floors, description) "
             "VALUES ('%s', '%s', '%s', %d, %d, '%s');",
             name, address, owner, capacity, floors, description);

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to add building: %s\n", err_msg);
        write(client, raspuns, strlen(raspuns));
        sqlite3_free(err_msg);
    } 
    else 
    {
        snprintf(raspuns, sizeof(raspuns), "Building '%s' added successfully.\n", name);
        write(client, raspuns, strlen(raspuns));
    }
}

void delete_building(int client, char *input) 
{
    char *err_msg = NULL;
    char name[256];
    char sql[512];
    char raspuns[256];
    bzero(name, 256);
    bzero(raspuns, 256);
    bzero(sql, 512);

    strncpy(name, input, sizeof(name) - 1);

    snprintf(sql, sizeof(sql), "DELETE FROM buildings WHERE name='%s';", name);

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to delete building '%s': %s\n", name, err_msg);
        sqlite3_free(err_msg);
    } 
    else 
    {
        if (sqlite3_changes(db) == 0) 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: No building found with name '%s'.\n", name);
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Building with name '%s' deleted.\n", name);
        }
    }

    write(client, raspuns, strlen(raspuns));
}

void view_buildings(int client, char *input) 
{
    char raspuns[1024];
    bzero(raspuns, 1024);

    sqlite3_stmt *stmt;
    char sql[1024];
    bzero(sql, 1024);
    strcpy(sql, "SELECT name, address, owner, capacity, floors, description FROM buildings ");

    char* p=strtok(input, " ");
    bool conditieAdaugata=false;
    if(p!=NULL)
    {
        strcat(sql, "WHERE ");
        while(p!=NULL)
        {
            if(conditieAdaugata) strcat(sql, " AND ");
            strcat(sql, p);
            conditieAdaugata=true;
            p=strtok(NULL, ",");
        }
    }
    strcat(sql, " ");
    strcat(sql, buildings_order);
    strcat(sql, ";");
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to retrieve buildings: %s\n", sqlite3_errmsg(db));
        write(client, raspuns, strlen(raspuns));
        return;
    }

    strcat(raspuns, "Buildings:\n");

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) 
    {
        char row[256];
        snprintf(row, sizeof(row),
                 "Name: %s, Address: %s, Owner: %s, Capacity: %d, Floors: %d, Description: %s\n",
                 sqlite3_column_text(stmt, 0),
                 sqlite3_column_text(stmt, 1),
                 sqlite3_column_text(stmt, 2),
                 sqlite3_column_int(stmt, 3),
                 sqlite3_column_int(stmt, 4),
                 sqlite3_column_text(stmt, 5));

        if (strlen(raspuns) + strlen(row) >= sizeof(raspuns)) 
        {
            write(client, raspuns, strlen(raspuns));
            bzero(raspuns, sizeof(raspuns));
        }
        strcat(raspuns, row);
    }

    write(client, raspuns, strlen(raspuns));
    sqlite3_finalize(stmt);
}

void modify_building(int client, char *input) 
{
    char *err_msg = NULL;
    bool nameAdded = false;
    char name[256] = "";
    char sql[2048] = "UPDATE buildings SET ";
    char raspuns[512];
    bool variableAdded = false;

    char *p = strtok(input, ",");
    while (p != NULL) 
    {
        while (*p == ' ') p++;
        if (strncmp(p, "name:", 5) == 0 && !nameAdded) 
        {
            strncpy(name, p + 5, sizeof(name) - 1);
            nameAdded = true;
        } 
        else if (strncmp(p, "address:", 8) == 0) 
        {
            if (variableAdded==true) strcat(sql, ", ");
            strcat(sql, "address='");
            strcat(sql, p + 8);
            strcat(sql, "'");
            variableAdded=true;
        } 
        else if (strncmp(p, "owner:", 6) == 0) 
        {
            if (variableAdded==true) strcat(sql, ", ");
            strcat(sql, "owner='");
            strcat(sql, p + 6);
            strcat(sql, "'");
            variableAdded=true;
        } 
        else if (strncmp(p, "capacity:", 9) == 0) 
        {
            if (variableAdded==true) strcat(sql, ", ");
            strcat(sql, "capacity=");
            strcat(sql, p + 9);
            variableAdded=true;
        } 
        else if (strncmp(p, "floors:", 7) == 0) 
        {
            if (variableAdded==true) strcat(sql, ", ");
            strcat(sql, "floors=");
            strcat(sql, p + 7);
            variableAdded=true;
        } 
        else if (strncmp(p, "description:", 12) == 0) 
        {
            if (variableAdded==true) strcat(sql, ", ");
            strcat(sql, "description='");
            strcat(sql, p + 12);
            strcat(sql, "'");
            variableAdded=true;
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: Not a valid field or a repeated field: %s\n", p);
            write(client, raspuns, strlen(raspuns));
            return;
        }
        p = strtok(NULL, ",");
    }

    if (!nameAdded) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Missing required field 'name'\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    if (variableAdded == false) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: No fields to modify provided\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    strcat(sql, " WHERE name='");
    strcat(sql, name);
    strcat(sql, "';");

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to modify building '%s': %s\n", name, err_msg);
        write(client, raspuns, strlen(raspuns));
        sqlite3_free(err_msg);
    } 
    else 
    {
        if (sqlite3_changes(db) == 0) 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: Building '%s' does not exist.\n", name);
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Building '%s' modified successfully.\n", name);
        }
        write(client, raspuns, strlen(raspuns));
    }
}

void add_room(int client, char *input) 
{
    char *err_msg = NULL;
    bool nameAdded = false, buildingNameAdded = false;
    char building_name[256] = "", name[256] = "", room_type[256] = "", owner[256] = "", floor[16]="", image_url[512] = "";
    int capacity = 0;
    char description[512] = "";
    char raspuns[512];

    char *p = strtok(input, ",");
    while (p != NULL) 
    {
        while (*p == ' ') p++;
        if (strncmp(p, "building_name:", 14) == 0 && !buildingNameAdded) 
        {
            strncpy(building_name, p + 14, sizeof(building_name) - 1);
            buildingNameAdded = true;
        } 
        else if (strncmp(p, "name:", 5) == 0 && !nameAdded) 
        {
            strncpy(name, p + 5, sizeof(name) - 1);
            nameAdded = true;
        } 
        else if (strncmp(p, "room_type:", 10) == 0) 
        {
            strncpy(room_type, p + 10, sizeof(room_type) - 1);
        } 
        else if (strncmp(p, "owner:", 6) == 0) 
        {
            strncpy(owner, p + 6, sizeof(owner) - 1);
        } 
        else if (strncmp(p, "floor:", 6) == 0) 
        {
            strncpy(floor, p + 6, sizeof(floor) - 1);
        } 
        else if (strncmp(p, "capacity:", 9) == 0) 
        {
            capacity = atoi(p + 9);
        } 
        else if (strncmp(p, "image_url:", 10) == 0) 
        {
            strncpy(image_url, p + 10, sizeof(image_url) - 1);
            if (!isImageValid(image_url)) 
            {
                snprintf(raspuns, sizeof(raspuns), "Error: File at '%s' is not a valid image.\n", image_url);
                write(client, raspuns, strlen(raspuns));
                return;
            }
        } 
        else if (strncmp(p, "description:", 12) == 0) 
        {
            strncpy(description, p + 12, sizeof(description) - 1);
        }
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: Not a valid field or a repeated field: %s\n", p);
            write(client, raspuns, strlen(raspuns));
            return;
        }
        p = strtok(NULL, ",");
    }

    if (!nameAdded || !buildingNameAdded) 
    {
        if (!buildingNameAdded) 
        {
            strcpy(raspuns, "Error: Missing 'building_name' field, which is required\n");
        } 
        else if (!nameAdded) 
        {
            strcpy(raspuns, "Error: Missing 'name' field, which is required\n");
        }
        write(client, raspuns, strlen(raspuns));
        return;
    }

    char sql_check[256];
    snprintf(sql_check, sizeof(sql_check), "SELECT COUNT(*) FROM buildings WHERE name = '%s';", building_name);
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql_check, -1, &stmt, 0) == SQLITE_OK) 
    {
        if (sqlite3_step(stmt) == SQLITE_ROW) 
        {
            int count = sqlite3_column_int(stmt, 0);
            if (count == 0) 
            {
                snprintf(raspuns, sizeof(raspuns), "Error: Building '%s' does not exist.\n", building_name);
                write(client, raspuns, strlen(raspuns));
                sqlite3_finalize(stmt);
                return;
            }
        }
        sqlite3_finalize(stmt);
    } 
    else 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to verify building existence.\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    char sql[2048];
    bzero(sql, 2048);
    snprintf(sql, sizeof(sql),
             "INSERT INTO rooms (building_name, name, room_type, floor, owner, capacity, image_url, description) "
             "VALUES ('%s', '%s', '%s', '%s', '%s', %d, '%s', '%s');",
             building_name, name, room_type, floor, owner, capacity, image_url, description);

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to add room: %s\n", err_msg);
        write(client, raspuns, strlen(raspuns));
        sqlite3_free(err_msg);
    } 
    else 
    {
        snprintf(raspuns, sizeof(raspuns), "Room '%s' added successfully in building '%s'.\n", name, building_name);
        write(client, raspuns, strlen(raspuns));
    }
}

void delete_room(int client, char *input) 
{
    char *err_msg = NULL;
    char name[256];
    char sql[512];
    char raspuns[256];
    bzero(name, 256);
    bzero(raspuns, 256);
    bzero(sql, 512);

    strncpy(name, input, sizeof(name) - 1);

    snprintf(sql, sizeof(sql), "DELETE FROM rooms WHERE name='%s';", name);

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to delete room '%s': %s\n", name, err_msg);
        sqlite3_free(err_msg);
    } 
    else 
    {
        if (sqlite3_changes(db) == 0) 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: No room found with name '%s'.\n", name);
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Room with name '%s' deleted.\n", name);
        }
    }
    write(client, raspuns, strlen(raspuns));
}

void view_rooms(int client, char *input) 
{
    char raspuns[1024];
    bzero(raspuns, 1024);

    sqlite3_stmt *stmt;
    char sql[2048];
    bzero(sql, 2048);
    strcpy(sql, "SELECT building_name, name, room_type, owner, capacity, floor, image_url, description FROM rooms ");

    char* p = strtok(input, " ");
    bool conditieAdaugata = false;
    if (p != NULL) 
    {
        strcat(sql, "WHERE ");
        while (p != NULL) 
        {
            if (conditieAdaugata) strcat(sql, " AND ");
            strcat(sql, p);
            conditieAdaugata = true;
            p = strtok(NULL, ",");
        }
    }
    strcat(sql, " ");
    strcat(sql, rooms_order);
    strcat(sql, ";");
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to retrieve rooms: %s\n", sqlite3_errmsg(db));
        write(client, raspuns, strlen(raspuns));
        return;
    }

    strcat(raspuns, "Rooms:\n");

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) 
    {
        char row[512];
        snprintf(row, sizeof(row),
                 "Building:%s, Name:%s, Room type:%s, Owner:%s, Capacity:%d, Floor:%s, Image URL:%s, Description:%s\n",
                 sqlite3_column_text(stmt, 0),
                 sqlite3_column_text(stmt, 1),
                 sqlite3_column_text(stmt, 2),
                 sqlite3_column_text(stmt, 3),
                 sqlite3_column_int(stmt, 4),
                 sqlite3_column_text(stmt, 5),
                 sqlite3_column_text(stmt, 6),
                 sqlite3_column_text(stmt, 7));

        if (strlen(raspuns) + strlen(row) >= sizeof(raspuns)) 
        {
            write(client, raspuns, strlen(raspuns));
            bzero(raspuns, sizeof(raspuns));
        }
        strcat(raspuns, row);
    }

    if (strlen(raspuns) > 0) 
    {
        write(client, raspuns, strlen(raspuns));
    }

    sqlite3_finalize(stmt);
}

void modify_room(int client, char *input) 
{
    char *err_msg = NULL;
    bool nameAdded = false;
    char name[256] = "";
    char building_name[256]="";
    char sql[2048] = "UPDATE rooms SET ";
    char raspuns[512];
    bool variableAdded = false;

    char *p = strtok(input, ",");
    while (p != NULL) 
    {
        while (*p == ' ') p++;
        if (strncmp(p, "name:", 5) == 0 && !nameAdded) 
        {
            strncpy(name, p + 5, sizeof(name) - 1);
            nameAdded = true;
        } 
        else if (strncmp(p, "building_name:", 14) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "building_name='");
            strcat(sql, p + 14);
            strcat(sql, "'");
            variableAdded = true;
        } 
        else if (strncmp(p, "room_type:", 10) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "room_type='");
            strcat(sql, p + 10);
            strcat(sql, "'");
            variableAdded = true;
        } 
        else if (strncmp(p, "owner:", 6) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "owner='");
            strcat(sql, p + 6);
            strcat(sql, "'");
            variableAdded = true;
        } 
        else if (strncmp(p, "capacity:", 9) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "capacity=");
            strcat(sql, p + 9);
            variableAdded = true;
        } 
        else if (strncmp(p, "floor:", 6) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "floor='");
            strcat(sql, p + 6);
            strcat(sql, "'");
            variableAdded = true;
        } 
        else if (strncmp(p, "image_url:", 10) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "image_url='");
            strcat(sql, p + 10);
            strcat(sql, "'");
            if (!isImageValid(p + 10)) 
            {
                snprintf(raspuns, sizeof(raspuns), "Error: File at '%s' is not a valid image.\n", p + 10);
                write(client, raspuns, strlen(raspuns));
                return;
            }
            variableAdded = true;
        } 
        else if (strncmp(p, "description:", 6) == 0) 
        {
            if (variableAdded) strcat(sql, ", ");
            strcat(sql, "description='");
            strcat(sql, p + 12);
            strcat(sql, "'");
            variableAdded = true;
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: Not a valid field or a repeated field: %s\n", p);
            write(client, raspuns, strlen(raspuns));
            return;
        }
        p = strtok(NULL, ",");
    }

    if (!nameAdded) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Missing required field 'name'\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    if (variableAdded == false) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: No fields to modify provided\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    strcat(sql, " WHERE name='");
    strcat(sql, name);
    strcat(sql, "';");

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) 
    {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to modify room '%s': %s\n", name, err_msg);
        write(client, raspuns, strlen(raspuns));
        sqlite3_free(err_msg);
    } 
    else 
    {
        if (sqlite3_changes(db) == 0) 
        {
            snprintf(raspuns, sizeof(raspuns), "Error: Room '%s' does not exist.\n", name);
        } 
        else 
        {
            snprintf(raspuns, sizeof(raspuns), "Room '%s' modified successfully.\n", name);
        }
        write(client, raspuns, strlen(raspuns));
    }
}

int isImageValid(char *image_path) 
{
    if (access(image_path, F_OK) != 0) 
    {
        return false;
    }

    const char *valid_extensions[] = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tiff"};
    const char *ext = strrchr(image_path, '.');

    if (ext == NULL) 
    {
        return false;
    }

    const char **extension = valid_extensions;
    while (*extension != NULL) 
    {
        if (strcasecmp(ext, *extension) == 0) 
        {
            return true;
        }
        extension++;
    }

    return false;
}

void order_buildings(int client, const char *input) {
    char order[256] = "ORDER BY ";
    char raspuns[256];

    if (input == NULL || strlen(input) == 0 || strspn(input, " ") == strlen(input)) {
        snprintf(raspuns, sizeof(raspuns), "Error: No input provided for ordering.\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    strncat(order, input, sizeof(order) - strlen(order) - 1);

    char sql_check[512];
    snprintf(sql_check, sizeof(sql_check), "SELECT * FROM buildings %s LIMIT 1;", order);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to validate ORDER BY clause: %s\n", sqlite3_errmsg(db));
        write(client, raspuns, strlen(raspuns));
        return;
    }

    sqlite3_finalize(stmt);
    strncpy(buildings_order, order, sizeof(buildings_order) - 1);
    buildings_order[sizeof(buildings_order) - 1] = '\0';

    snprintf(raspuns, sizeof(raspuns), "New order clause: %s\n", buildings_order);
    write(client, raspuns, strlen(raspuns));
}

void order_rooms(int client, const char *input) {
    char order[256] = "ORDER BY ";
    char raspuns[256];

    if (input == NULL || strlen(input) == 0 || strspn(input, " ") == strlen(input)) {
        snprintf(raspuns, sizeof(raspuns), "Error: No input provided for ordering.\n");
        write(client, raspuns, strlen(raspuns));
        return;
    }

    strncat(order, input, sizeof(order) - strlen(order) - 1);

    char sql_check[512];
    snprintf(sql_check, sizeof(sql_check), "SELECT * FROM rooms %s LIMIT 1;", order);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        snprintf(raspuns, sizeof(raspuns), "Error: Failed to validate ORDER BY clause: %s\n", sqlite3_errmsg(db));
        write(client, raspuns, strlen(raspuns));
        return;
    }

    sqlite3_finalize(stmt);

    strncpy(rooms_order, order, sizeof(rooms_order) - 1);
    rooms_order[sizeof(rooms_order) - 1] = '\0';

    snprintf(raspuns, sizeof(raspuns), "New order clause: %s\n", rooms_order);
    write(client, raspuns, strlen(raspuns));
}
