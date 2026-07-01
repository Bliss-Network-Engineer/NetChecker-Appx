#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <pthread.h>
#include <stdbool.h>

//function declarations
void app_interface();
void edit_deviceList();
void Add_Device();
void view();
void delete_entry();
int searchbyip(char parameter[15]);
void *FunctionToCheckDevices();
void send_alert(char name[50], char ip[15], char location[50]);
void retry_edit();
void save_file();
void open_file();
void read_line(char *buffer, int size);
void flush_stdin(void);
char ask_yes_no(const char *prompt);
int  read_choice(void);


//presentation
//input/choice
//processing
//output
//continuity
//multithreading

//for a in range 0-i

#define MAX_DEVICES 100 
int i=0; 
bool stop_flag = false;
 
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t monitor_thread;
bool monitor_started = false;

//use a function to update the value of i based on number of devices in the list on launch


struct Devices{
    char name[50];
    char ip[15];
    char location[50];
    char status[20]; //{"Active", "Not-Active", "Unknown"} the status value is updated by the status function;
};



 
// ---- small input helpers ----
 
void flush_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// Reads a line into buffer, strips trailing newline, bounds to size.
void read_line(char *buffer, int size) {
    if (fgets(buffer, size, stdin) != NULL) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        } else {
            // input longer than buffer: drain the rest of the line
            flush_stdin();
        }
    } else {
        buffer[0] = '\0';
    }
}
 
// Repeats a prompt until the user answers y or n, returns 'y' or 'n'.
char ask_yes_no(const char *prompt) {
    char buf[16];
    while (1) {
        printf("%s", prompt);
        read_line(buf, sizeof(buf));
        if (buf[0] == 'y' || buf[0] == 'Y') return 'y';
        if (buf[0] == 'n' || buf[0] == 'N') return 'n';
        printf("Please type y or n.\n");
    }
}


//Commands Handler

struct Devices Device_List[MAX_DEVICES];

//void input_handler(){}

int Customdelete(int id){
    
    pthread_mutex_lock(&list_lock);
    for (int x = id; x < i - 1; x++) {
        Device_List[x] = Device_List[x + 1];
    }
    i--;
    pthread_mutex_unlock(&list_lock);
    save_file();
    return 0;
}

int searchbyip(char parameter[15]){
        //search with name or ip address
        //use regular expression to identify search parameter
        pthread_mutex_lock(&list_lock);
    if (i == 0) {
        pthread_mutex_unlock(&list_lock);
        printf("No Device in the List.\n");
        return -1;
    }
    for (int b = 0; b < i; b++) {
        if (strcmp(Device_List[b].ip, parameter) == 0) {
            printf("Device Found!!!\n");
            printf("Device-Name \t\t\t IP-Address \t\t\t Location \t\t\t Status \n\n");
            printf("%s\t\t\t%s\t\t\t%s\t\t\t%s\n",
                   Device_List[b].name, Device_List[b].ip,
                   Device_List[b].location, Device_List[b].status);
            pthread_mutex_unlock(&list_lock);
            return b;
        }
    }
    pthread_mutex_unlock(&list_lock);
    printf("Device not found, verify that the ip address is correct and try again.\n");
    return -1;
}

void view(){
    pthread_mutex_lock(&list_lock);
    printf("Device-Name \t\t\t IP-Address \t\t\t Location \t\t\t Status \n\n");
    if (i == 0) {
        printf("No device has been added to the list.\n");
    } else {
        for (int a = 0; a < i; a++) {
            printf("%s\t\t\t  %s\t\t\t  %s\t\t\t  %s\n\n",
                   Device_List[a].name, Device_List[a].ip,
                   Device_List[a].location, Device_List[a].status);
        }
    }
    pthread_mutex_unlock(&list_lock);
}

void delete_entry(){
           char parameter[15];
    printf("Enter the IP address of the device: ");
    read_line(parameter, sizeof(parameter));
 
    int a = searchbyip(parameter);
    if (a < 0) {
        printf("Device not found!!!\n");
        return;
    }
 
    char confirm = ask_yes_no("\nAre you sure you want to delete this device? Enter y or n: ");
    if (confirm == 'y') {
        int dd = Customdelete(a);
        if (dd == 0) {
            printf("Device successfully deleted!!!\n");
        } else {
            printf("An Error Occurred!!!\n");
        }
        view();
    } else {
        printf("No changes made.\n\n");
    }
}




//-------MENU-------

int read_choice(void) {
    char buf[16];
    printf("Type in Your Choice: ");
    read_line(buf, sizeof(buf));
    return atoi(buf); // returns 0 on non-numeric input, which falls through to "Invalid Input"
}


void app_interface(){
    printf("\n_____________________Welcome to the NetChecker App________________\n");
    printf("Make your choice of operation, by typing the corresponding number below\n");
    printf("1. Add a New Device\n"
           "2. Edit Device Information\n"
           "3. Delete a Device\n"
           "4. Search for a Device\n"
           "5. Start Status Checker\n"
           "6. View All Entries\n"
           "7. Exit\n");
 
    int choice = read_choice();
 
    switch (choice) {
        case 1:
            Add_Device();
            break;
        case 2:
            edit_deviceList();
            break;
        case 3:
            delete_entry();
            break;
        case 4: {
            char address[15];
            printf("\nEnter the IP address: ");
            read_line(address, sizeof(address));
            searchbyip(address);
            break;
        }
        case 5:
            if (!monitor_started) {
                if (pthread_create(&monitor_thread, NULL, FunctionToCheckDevices, NULL) != 0) {
                    printf("Failed to create monitor thread\n");
                } else {
                    monitor_started = true;
                    printf("Monitor has been started\n");
                }
            } else {
                printf("Monitor is already running.\n");
            }
            break;
        case 6:
            view();
            break;
        case 7: {
            char bc = ask_yes_no("Are you sure you want to exit? y or n: ");
            if (bc == 'y') {
                stop_flag = true;
                if (monitor_started) {
                    pthread_join(monitor_thread, NULL); // let the monitor exit its loop cleanly
                }
                exit(0);
            }
            // 'n' -> just fall through, menu loops again
            break;
        }
        default:
            printf("Invalid Input!!!\n");
            break;
    }
}

void Add_Device(){

    /* if(i>=100){
        printf("List is full!!\n");
        return;
        //initiating a list expansion will be best
    }
    printf("Enter Desired Name: ");
    scanf("%49s", Device_List[i].name); //newly added
    //fgets(Device_List[i].name, 50, stdin);
    printf("\nEnter the IP address of the device: ");
    scanf("%14s", Device_List[i].ip); //changed something here
    printf("\nEnter the location of the device: ");
    scanf("%49s", Device_List[i].location);
    strcpy(Device_List[i].status, "Unknown");
    //fgets(Device_List[i].location, 50, stdin);
    i++;  //increase number of entries in the list
    save_file();
    printf("Device Successfully added!!!\n");
    view();
    app_interface(); */


    pthread_mutex_lock(&list_lock);
    if (i >= MAX_DEVICES) {
        pthread_mutex_unlock(&list_lock);
        printf("List is full!!\n");
        return;
    }
    pthread_mutex_unlock(&list_lock);
 
    char name[50], ip[15], location[50];
 
    printf("Enter Desired Name: ");
    read_line(name, sizeof(name));
    printf("Enter the IP address of the device: ");
    read_line(ip, sizeof(ip));
    printf("Enter the location of the device: ");
    read_line(location, sizeof(location));
 
    pthread_mutex_lock(&list_lock);
    if (i >= MAX_DEVICES) {
        pthread_mutex_unlock(&list_lock);
        printf("List is full!!\n");
        return;
    }
    strncpy(Device_List[i].name, name, sizeof(Device_List[i].name) - 1);
    Device_List[i].name[sizeof(Device_List[i].name) - 1] = '\0';
    strncpy(Device_List[i].ip, ip, sizeof(Device_List[i].ip) - 1);
    Device_List[i].ip[sizeof(Device_List[i].ip) - 1] = '\0';
    strncpy(Device_List[i].location, location, sizeof(Device_List[i].location) - 1);
    Device_List[i].location[sizeof(Device_List[i].location) - 1] = '\0';
    strcpy(Device_List[i].status, "Unknown");
    i++;
    pthread_mutex_unlock(&list_lock);
 
    save_file();
    printf("Device Successfully added!!!\n");
    view();


    //add a funtion to allow adding from a list or file
}

void *FunctionToCheckDevices(void *arg){
        //ICMP socket based checker or system call based checker
    
        (void)arg;
 
    while (!stop_flag) {
        pthread_mutex_lock(&list_lock);
        int count = i;
        pthread_mutex_unlock(&list_lock);
 
        if (count == 0) {
            Sleep(2000);
            continue;
        }
 
        for (int a = 0; a < count && !stop_flag; a++) {
            char ip_copy[15];
            char name_copy[50];
            char location_copy[50];
 
            pthread_mutex_lock(&list_lock);
            if (a >= i) { // list shrank since we grabbed `count`
                pthread_mutex_unlock(&list_lock);
                break;
            }
            strcpy(ip_copy, Device_List[a].ip);
            strcpy(name_copy, Device_List[a].name);
            strcpy(location_copy, Device_List[a].location);
            pthread_mutex_unlock(&list_lock);
 
            char command[100];
            sprintf(command, "ping -n 2 -w 1000 %s >nul", ip_copy);
            int feedback = system(command);
 
            pthread_mutex_lock(&list_lock);
            if (a < i && strcmp(Device_List[a].ip, ip_copy) == 0) {
                strcpy(Device_List[a].status, feedback == 0 ? "Active" : "Not_Active");
            }
            pthread_mutex_unlock(&list_lock);
 
            if (feedback != 0) {
                send_alert(name_copy, ip_copy, location_copy);
            }
        }
        save_file();
        Sleep(3000);
    }
    return NULL;


   //icmp socket based implementation
   /*  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed with error: %d\n", WSAGetLastError());
        return;
    }                   

    int connection = connect(sock, (struct sockaddr *)&Device_List[a].ip, sizeof(Device_List[a].ip));
    if (connection == SOCKET_ERROR) {
        printf("Connection failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        return;
    } */
    

}

/* void retry_edit(){
    char a;
    printf("Do you want to retry edit? Type y or n: ");
    scanf(" %c", &a);
    if(a == 'y'){edit_deviceList();}
    else if(a == 'n'){app_interface();}
    else {
        printf("Invalid input!!!\n"); 
        retry_edit();
    } */
}

void edit_deviceList(){
        /* char buff[10];
        char address[15];
        printf("\n Enter the IP address of the device: ");
        scanf(" %s", address); //there is need to remove additional spaces before using parameter
        int a = searchbyip(address);
    if (a>=0)
    {
        printf("What do you want to edit? Type name, ip, or location(Don't add any extra space or character): ");
        scanf(" %9s", &buff);
        //use regular expression to control what enters the buff
        if(strcmp(buff,"name")==0){
            printf("Enter the new device-name: ");
            fgets(Device_List[a].name, 50, stdin);
        }
        else if (strcmp(buff, "location")==0){
            printf("Enter the new device-location: ");
            fgets(Device_List[a].location, 50, stdin);
        }
        else if(strcmp(buff, "ip")==0){
            printf("Enter the new device-ip: ");
            scanf(" %s", Device_List[a].ip);
        }
        else{
            printf("Invalid Input!!!\n");
            retry_edit();
        }

        printf("\nDevice information successfully edited\n");
        printf("Device-Name \t\t\t IP-Address \t\t\t Location \t\t\t Status \n\n");
        printf("%s\t\t\t%s\t\t\t%s\t\t\t%s\n", Device_List[a].name, Device_List[a].ip, Device_List[a].location, Device_List[a].status);
        save_file();
        app_interface();
    }
    else{
        printf("\nDevice Not Found!!!\n");
        retry_edit();
        save_file();
    }     */

        char address[15];
    printf("\nEnter the IP address of the device: ");
    read_line(address, sizeof(address));
 
    int a = searchbyip(address);
    if (a < 0) {
        printf("\nDevice Not Found!!!\n");
        return;
    }
 
    char buff[10];
    printf("What do you want to edit? Type name, ip, or location: ");
    read_line(buff, sizeof(buff));
 
    pthread_mutex_lock(&list_lock);
    // re-check index is still valid in case list changed between search and edit
    if (a >= i) {
        pthread_mutex_unlock(&list_lock);
        printf("Device list changed, please try again.\n");
        return;
    }
 
    if (strcmp(buff, "name") == 0) {
        pthread_mutex_unlock(&list_lock);
        printf("Enter the new device-name: ");
        char tmp[50];
        read_line(tmp, sizeof(tmp));
        pthread_mutex_lock(&list_lock);
        strncpy(Device_List[a].name, tmp, sizeof(Device_List[a].name) - 1);
        Device_List[a].name[sizeof(Device_List[a].name) - 1] = '\0';
    } 
    
    else if (strcmp(buff, "location") == 0) {
        pthread_mutex_unlock(&list_lock);
        printf("Enter the new device-location: ");
        char tmp[50];
        read_line(tmp, sizeof(tmp));
        pthread_mutex_lock(&list_lock);
        strncpy(Device_List[a].location, tmp, sizeof(Device_List[a].location) - 1);
        Device_List[a].location[sizeof(Device_List[a].location) - 1] = '\0';
    } 
    
    else if (strcmp(buff, "ip") == 0) {
        pthread_mutex_unlock(&list_lock);
        printf("Enter the new device-ip: ");
        char tmp[15];
        read_line(tmp, sizeof(tmp));
        pthread_mutex_lock(&list_lock);
        strncpy(Device_List[a].ip, tmp, sizeof(Device_List[a].ip) - 1);
        Device_List[a].ip[sizeof(Device_List[a].ip) - 1] = '\0';
    } 
    
    else {
        pthread_mutex_unlock(&list_lock);
        printf("Invalid Input!!!\n");
        return;
    }
 
    printf("\nDevice information successfully edited\n");
    printf("Device-Name \t\t\t IP-Address \t\t\t Location \t\t\t Status \n\n");
    printf("%s\t\t\t%s\t\t\t%s\t\t\t%s\n",
           Device_List[a].name, Device_List[a].ip,
           Device_List[a].location, Device_List[a].status);
    pthread_mutex_unlock(&list_lock);
 
    save_file();
}



void send_alert(char name[50], char ip[15], char location[50]){
        //use a message box
        char Message[200];
        //for(int a=0; a<i; a++){
        //if(strcpy(Device_List[a].status, "Not Active") == 0){
            sprintf(Message, "Device named %s with IP address %s at %s is Not Active", name, ip, location);
            MessageBox(NULL, Message, "Alert!!!", MB_ICONEXCLAMATION | MB_OK);
            Beep(1000, 500);
        //}
      // }
        //ALert title, message body, severity, beep
}

void save_file(){
    //Save Device list to csv file or db on every change made
    pthread_mutex_lock(&list_lock);
    
    FILE *f = fopen("Devices_List.csv", "w");
    
    if (f == NULL) {
        pthread_mutex_unlock(&list_lock);
        printf("Error opening file!\n");
        return;
    }
    
    fprintf(f, "Device-Name,IP-Address,Location,Status\n");
    for (int a = 0; a < i; a++) {
        fprintf(f, "%s,%s,%s,%s\n",
                Device_List[a].name, Device_List[a].ip,
                Device_List[a].location, Device_List[a].status);
    }

    fclose(f);
    pthread_mutex_unlock(&list_lock);
}

void open_file(){
    //On starting the app, load devices from file
    FILE *f = fopen("Devices_List.csv", "r");
    if (f == NULL) {
        printf("No existing device list found. Starting with an empty list.\n");
        return;
    }
    char line[300];
    fgets(line, sizeof(line), f); // skip header
    while (fgets(line, sizeof(line), f) && i < MAX_DEVICES) {
        int fields = sscanf(line, "%49[^,],%14[^,],%49[^,],%19[^\r\n]",
                             Device_List[i].name, Device_List[i].ip,
                             Device_List[i].location, Device_List[i].status);
        if (fields == 4) {
            i++;
        }
        // malformed rows are silently skipped rather than corrupting the array
    }
    fclose(f);
}

int main(){
    open_file();
    //WSAStartup(MAKEWORD(2,2), &wsa);

while(1){
        app_interface();
        //save_file();
    }
    return 0;
}
