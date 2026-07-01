#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <pthread.h>

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
void exit();
void save_file();
void open_file();


//presentation
//input/choice
//processing
//output
//continuity
//multithreading

//for a in range 0-i
int function_id_monitor;
int i=0; 
boolean stop_flag =0;

//use a function to update the value of i based on number of devices in the list on launch


struct Devices{
    char name[50];
    char ip[15];
    char location[50];
    char status[20]; //{"Active", "Not-Active", "Unknown"} the status value is updated by the status function;
};

//Commands Handler

struct Devices Device_List[100];

//void input_handler(){}

int Customdelete(int id){
    
    for(int x = id; x < i-1; x++){
        strcpy(Device_List[x].name, Device_List[x+1].name);
        strcpy(Device_List[x].ip, Device_List[x+1].ip);
        strcpy(Device_List[x].location, Device_List[x+1].location);
        strcpy(Device_List[x].status, Device_List[x+1].status);
    }   
        i--;
        save_file();
        return 0;
}

int searchbyip(char parameter[15]){
        //search with name or ip address
        //use regular expression to identify search parameter
        if(i==0){
            printf("No Device in the List. \n");
            return -1;
        }
        for(int b=0; b<i; b++){
            if(strcmp(Device_List[b].ip, parameter) == 0){
                printf("Device Found!!!\n");
                printf("Device-Name \t\t\t IP-Address \t\t\t Location \t\t\t Status \n\n");
                printf("\n%s\t\t\t%s\t\t\t%s\t\t\t%s\n", Device_List[b].name, Device_List[b].ip, Device_List[b].location, Device_List[b].status);
                return b;
            }
           
        }
        printf("Device not found, verify that the ip address is correct and try again.\n");
                return -1;
}

void view(){
    printf("Device-Name \t\t\t IP-Address \t\t\t Location \t\t\t Status \n\n");

    if(i==0){
            printf("No device has been added to the list.\n");
    }

    else{
     for(int a=0; a<i; a++){
        else{
        printf("%s\t\t\t  %s\t\t\t  %s\t\t\t  %s\n\n", Device_List[a].name, Device_List[a].ip, Device_List[a].location, Device_List[a].status);
        }
       }
    }
    app_interface();
}

void delete_entry(){
            char parameter[15];
            char jj;
            printf("Enter the IP address of the device: ");
            scanf("%s", parameter);
            int a = searchbyip(parameter);
            //verify whether device not found will be printed after executing the search function
        if(a>=0){
            printf("\nAre you sure you want to delete this device? Enter y or n: ");
            scanf(" %c", &jj);
            if(jj=='y'){
                //create a funtion to automatically delete and readjust the size of the array
                int dd = Customdelete(a);
                if(dd==0){
                    printf("Device successfully deleted!!!\n");
                }
                else{
                    printf("An Error Occurred!!!\n");
                    delete_entry();
                }
                view();

            }
            else if(jj=='n'){
                printf("No changes made.\n\n");
                view();
                app_interface();
            };
        }
        else{
            printf("Device not found!!!\n");
            app_interface();
        }
}

void app_interface(){
    int choice;
    printf("\n_____________________Welcome to the NetChecker App________________\n");
    printf("Make your choice of operation, by typing the coresponding number below\n");
    printf("1. Add a New Device\n"
        "2. Edit Device Information\n"
        "3. Delete a Device\n"
        "4. Search for a Device\n"
        "5. Start Status Checker\n"
        "6. View All Entries\n"
        "7. Exit\n"
    );
    printf("Type in Your Choice: ");
    scanf("%d", &choice);

    if (choice == 1){
        Add_Device();
    }
    else if(choice == 2){
        edit_deviceList();
    }
    else if(choice == 3){
        delete_entry();
    }
    else if(choice == 4){
        char address[15];
        printf("\n Enter the IP address: ");
        scanf("%s", address);
        searchbyip(address);
    }
    else if(choice == 5){
        printf("Monitor has been started");
    }

    else if(choice == 6){
        view();
    }

else if(choice == 7){

    //before exiting confirm if the choice is corrct
    //printf("Are you sure you want to exit?  y or n");

       stop_flag = 1;
    }

    else{
        printf("Invalid Input!!!\n");
        app_interface();
       
    }
}

//void exit(){}

void Add_Device(){

    if(i>=100){
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
    app_interface();

    //add a funtion to allow adding from a list or file
}

void *FunctionToCheckDevices(){
        //ICMP socket based checker or system call based checker
        if(i==0){
            printf("No Device has been added yet!!!\n");
            app_interface();
        }
    while(1){
    for(int a=0; a<i; a++){
        char command[100];
        sprintf(command, "ping -n 4 %s", Device_List[a].ip);
        int feedback = system(command); //this is a system call based checker
        if (feedback == 0){
            strcpy(Device_List[a].status, "Active");
        }
        else{
            strcpy(Device_List[a].status, "Not_Active");
            send_alert(Device_List[a].name, Device_List[a].ip, Device_List[a].location);
        }
        save_file();
        Sleep(3000);
    }
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

void retry_edit(){
    char a;
    printf("Do you want to retry edit? Type y or n: ");
    scanf(" %c", &a);
    if(a == 'y'){edit_deviceList();}
    else if(a == 'n'){app_interface();}
    else {
        printf("Invalid input!!!\n"); 
        retry_edit();
    }
}

void edit_deviceList(){
        char buff[10];
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
    }    
}

void send_alert(char name[50], char ip[15], char location[50]){
        //use a message box
        char Message[100];
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
    FILE *f = fopen("Devices_List.csv", "w");
    if (f == NULL) {  
        printf("Error opening file!\n"); 
        return; 
    }   
    fprintf(f, "Device-Name,IP-Address,Location,Status\n");
    for(int a=0; a<i; a++){ 
        fprintf(f, "%s,%s,%s,%s\n", Device_List[a].name, Device_List[a].ip, Device_List[a].location, Device_List[a].status);
    }
    fclose(f);
}

void open_file(){
    //On starting the app, load devices from file
    FILE *f = fopen("Devices_List.csv", "r");
    if (f == NULL) {
        printf("No existing device list found. Starting with an empty list.\n");
        return;
    }
    char line[200];
    fgets(line, sizeof(line), f); // Skip header line
    while (fgets(line, sizeof(line), f)) {  
        sscanf(line, "%[^,],%[^,],%[^,],%[^,]", Device_List[i].name, Device_List[i].ip, Device_List[i].location, Device_List[i].status);
        i++;
    }
    fclose(f);

}

int main(){
    open_file();
    WSAStartup(MAKEWORD(2,2), &wsa);
    
    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, (void *)FunctionToCheckDevices, NULL) != 0) {
        printf("Failed to create monitor thread\n");
        return 1;
    }
    while(1){
        app_interface();
        //save_file();
    }
    //return 0;
}
