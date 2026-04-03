#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>


//function declarations
void app_interface();
void edit_deviceList();
void Add_Device();
void view();
void delete_entry();
int searchbyip(char parameter[15]);
void FunctionToCheckDevices();
void alert();
void retry_edit();
void exit();



//presentation
//input/choice
//processing
//output
//continuity
//multithreading

//for a in range 0-i
int function_id_monitor;
int i=0;


struct Devices{
    char name[50];
    char ip[15];
    char location[50];
    char status[10]; //{"Active", "Not-Active", "Unknown"} the status value is updated by the status function;
};

//Commands Handler

struct Devices Device_List[100];

//void input_handler(){}

int Customdelete(int id){
    
    for(int x = id; x < i; x++){
        strcpy(Device_List[x].name, Device_List[x+1].name);
        strcpy(Device_List[x].ip, Device_List[x+1].ip);
        strcpy(Device_List[x].location, Device_List[x+1].location);
        strcpy(Device_List[x].status, Device_List[x+1].status);
    }
        return 0;
}

int searchbyip(char parameter[15]){
        //search with name or ip address
        //use regular expression to identify search parameter
        for(int b=0; b<i; b++){
            if(strcmp(Device_List[b].ip, parameter) == 0){
                printf("Device Found!!!\n");
                printf("Device-Name \t\t\t\t IP-Address \t\t\t\t Location \t\t\t\t Status \n\n");
                printf("%s\t\t\t%s\t\t\t%s\t\t\t%s\n", Device_List[b].name, Device_List[b].ip, Device_List[b].location, Device_List[b].status);
                return b;
            }
            else{
                printf("Device not found, verify that the ip address is correct and try again.");
                return -1;
            }
        }

}

void view(){
    printf("Device-Name \t\t\t\t IP-Address \t\t\t\t Location \t\t\t\t Status \n\n");
    for(int a=0; a<i; a++){
        if(i==0){
            printf("No device has been added to the list.\n");
        }
        else{
        printf("%s\t\t\t%s\t\t\t%s\t\t\t%s\n", Device_List[a].name, Device_List[a].ip, Device_List[a].location, Device_List[a].status);
        }
    }
}

void delete_entry(){
            char parameter[15];
            char jj;
            printf("Enter the IP address of the device: \n");
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
                    printf("Device successfully deleted!!!");
                }
                else{
                    printf("An Error Occurred!!!");
                    delete_entry();
                }
        
                view();

            }
            else if(jj=='n'){
                app_interface();
            };
        }
        else{
            printf("Device not found!!!");
            app_interface();
        }
}

void app_interface(){
    int choice;
    printf("_____________________Welcome to the NetChecker App________________\n");
    printf("Make your choice of operation, by typing the coresponding number below\n");
    printf("1. Add a New Device\n"
        "2. Edit Device Information\n"
        "3. Delete a Device\n"
        "4. Search for a Device\n"
        "5. Start Status Checker\n"
    );
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
        FunctionToCheckDevices();
    }
    else{
        printf("Invalid Input!!!");
        app_interface();
    }
}

//void exit(){}

void Add_Device(){
    printf("Enter Desired Name: ");
    fgets(Device_List[i].name, 50, stdin);
    printf("\nEnter the IP address of the device: ");
    scanf("%s", &Device_List[i].ip);
    printf("\nEnter the location of the device: ");
    fgets(Device_List[i].location, 50, stdin);
    i++;
    //add a funtion to allow adding from a list or file
}

void FunctionToCheckDevices(){
        //ICMP socket based checker or system call based checker
    for(int a=0; a<i; a++){
        char command[100];
        sprintf(command, "ping -n 1 %s", Device_List[a].ip);
        system(command); //this is a system call based checker
    }


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
    scanf("%c", &a);
    if(a == 'y'){edit_deviceList();}
    else if(a == 'n'){app_interface();}
    else {
        printf("Invalid input!!!"); 
        retry_edit();
    }
}

void edit_deviceList(){
        char buff[7];
        char address[15];
        printf("\n Enter the IP address of the device: ");
        scanf("%s", address); //there is need to remove additional spaces before using parameter
        int a = searchbyip(address);
    if (a>=0)
    {
        printf("What do you want to edit? Type name, ip, or location(Don't add any extra space or character): ");
        scanf("%s", &buff);
        if(buff=="name"){
            printf("Enter the new device-name: ");
            fgets(Device_List[a].name, 50, stdin);
        }
        else if (buff=="location"){
            printf("Enter the new device-location: ");
            fgets(Device_List[a].location, 50, stdin);
        }
        else if(buff=="ip"){
            printf("Enter the new device-ip: ");
            scanf("%s", Device_List[a].ip);
        }
        else{
            printf("Invalid Input!!!\n");
            retry_edit();
        }

        printf("Device information successfully edited");
        printf("Device-Name \t\t\t\t IP-Address \t\t\t\t Location \t\t\t\t Status \n\n");
        printf("%s\t\t\t%s\t\t\t%s\t\t\t%s\n", Device_List[a].name, Device_List[a].ip, Device_List[a].location, Device_List[a].status);
        app_interface();
    }
    else{
        printf("Device Not Found!!!");
        retry_edit();
    }    
}

void alert(){}





int main(){
    app_interface();
    return 0;
}