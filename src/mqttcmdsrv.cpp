//Disclaimer: this code is taken from https://github.com/LiamBindle/MQTT-C.git
//e.g run this command as: /usr/bin/mqttcmdsrv 172.29.1.1 1883 domoticz/out /etc/mqttcmdtrig.json
/**
 * @file
 * A simple subscriber program that performs automatic reconnections.
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <mqtt.h>
#include <string>
#include "posix_sockets.h"
#include <cjson/cJSON.h>
#include <deque>
/**
 * @brief A structure that I will use to keep track of some data needed
 *        to setup the connection to the broker.
 *
 * An instance of this struct will be created in my \c main(). Then, whenever
 * \ref reconnect_client is called, this instance will be passed.
 */
struct reconnect_state_t {
    const char* hostname;
    const char* port;
    const char* topic;
    uint8_t* sendbuf;
    size_t sendbufsz;
    uint8_t* recvbuf;
    size_t recvbufsz;
};
struct TrigEntry
{
        int Idx;
        std::string Key;
        std::string Value;
        std::string Trig;
        std::string Out;
        std::string Custm;
public:
        TrigEntry(int idx,std::string key,std::string value, std::string trig,std::string out,std::string custm) :Idx(idx),Key(key),Value(value),Trig(trig),Out(out),Custm(custm){}
};
std::deque<TrigEntry> TrigEntryList;
/**
 * @brief My reconnect callback. It will reestablish the connection whenever
 *        an error occurs.
 */
void reconnect_client(struct mqtt_client* client, void **reconnect_state_vptr);

/**
 * @brief The function will be called whenever a PUBLISH message is received.
 */
void publish_callback(void** unused, struct mqtt_response_publish *published);

/**
 * @brief The client's refresher. This function triggers back-end routines to
 *        handle ingress/egress traffic to the broker.
 *
 * @note All this function needs to do is call \ref __mqtt_recv and
 *       \ref __mqtt_send every so often. I've picked 100 ms meaning that
 *       client ingress/egress traffic will be handled every 100 ms.
 */
void* client_refresher(void* client);

/**
 * @brief Safelty closes the \p sockfd and cancels the \p client_daemon before \c exit.
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon);

int IsValidFile(const char* filepath);
int parseJsonConfig(const char* conffile,std::deque<TrigEntry> &TriggerList);

int main(int argc, const char *argv[])
{
    const char* addr;
    const char* port;
    const char* topic;
    const char* conffile;

    /* get address (argv[1] if present) */
    if (argc > 1) {
        addr = argv[1];
    } else {
        addr = "test.mosquitto.org";
    }

    /* get port number (argv[2] if present) */
    if (argc > 2) {
        port = argv[2];
    } else {
        port = "1883";
    }

    /* get the topic name to publish */
    if (argc > 3) {
        topic = argv[3];
    } else {
        topic = "datetime";
    }

    /* get the json-config file for triggers */
    if (argc > 4) {
        conffile = argv[4];
    } else {
        conffile = "mqttcmdtrig.json";
    }

    if(IsValidFile(conffile) != 0 )
    {
        printf("missing json config file!!! exiting..\n");
        exit_example(EXIT_FAILURE, -1, NULL);
    }
    if(parseJsonConfig(conffile,TrigEntryList) != 0 )
    {
        printf("Unable parse %s!!! exiting..\n",conffile);
        exit_example(EXIT_FAILURE, -1, NULL);
    }


    /* build the reconnect_state structure which will be passed to reconnect */
    struct reconnect_state_t reconnect_state;
    reconnect_state.hostname = addr;
    reconnect_state.port = port;
    reconnect_state.topic = topic;
    uint8_t sendbuf[2048];
    uint8_t recvbuf[1024];
    reconnect_state.sendbuf = sendbuf;
    reconnect_state.sendbufsz = sizeof(sendbuf);
    reconnect_state.recvbuf = recvbuf;
    reconnect_state.recvbufsz = sizeof(recvbuf);

    /* setup a client */
    struct mqtt_client client;

    mqtt_init_reconnect(&client,
                        reconnect_client, &reconnect_state,
                        publish_callback
    );

    /* start a thread to refresh the client (handle egress and ingree client traffic) */
    pthread_t client_daemon;
    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_example(EXIT_FAILURE, -1, NULL);

    }

    /* start publishing the time */
    printf("%s listening for '%s' messages.\n", argv[0], topic);
    //printf("Press ENTER to inject an error.\n");
    printf("Press CTRL-D to exit.\n\n");

    /* block */
    while(fgetc(stdin) != EOF) {
        //printf("Injecting error: \"MQTT_ERROR_SOCKET_ERROR\"\n");
        //client.error = MQTT_ERROR_SOCKET_ERROR;
    }

    /* disconnect */
    printf("\n%s disconnecting from %s\n", argv[0], addr);
    sleep(1);

    /* exit */
    exit_example(EXIT_SUCCESS, client.socketfd, &client_daemon);
}

void reconnect_client(struct mqtt_client* client, void **reconnect_state_vptr)
{
    struct reconnect_state_t *reconnect_state = *((struct reconnect_state_t**) reconnect_state_vptr);

    /* Close the clients socket if this isn't the initial reconnect call */
    if (client->error != MQTT_ERROR_INITIAL_RECONNECT) {
        close(client->socketfd);
    }

    /* Perform error handling here. */
    if (client->error != MQTT_ERROR_INITIAL_RECONNECT) {
        printf("reconnect_client: called while client was in error state \"%s\"\n",
               mqtt_error_str(client->error)
        );
    }

    /* Open a new socket. */
    int sockfd = open_nb_socket(reconnect_state->hostname, reconnect_state->port);
    if (sockfd == -1) {
        perror("Failed to open socket: ");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    /* Reinitialize the client. */
    mqtt_reinit(client, sockfd,
                reconnect_state->sendbuf, reconnect_state->sendbufsz,
                reconnect_state->recvbuf, reconnect_state->recvbufsz
    );

    /* Create an anonymous session */
    const char* client_id = NULL;
    /* Ensure we have a clean session */
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    /* Send connection request to the broker. */
    mqtt_connect(client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    /* Subscribe to the topic. */
    mqtt_subscribe(client, reconnect_state->topic, 0);
}

void exit_example(int status, int sockfd, pthread_t *client_daemon)
{
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}

void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    /* note that published->topic_name is NOT null-terminated (here we'll change it to a c-string) */
    char* topic_name = (char*) malloc(published->topic_name_size + 1);
    char* topic_value = (char*) malloc(published->application_message_size + 1);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    memcpy(topic_value, published->application_message, published->application_message_size);
    topic_value[published->application_message_size] = '\0';

    //printf("Received publish('%s'): %s\n", topic_name, (const char*) topic_value);//published->application_message);
    //parse json object and taks action as per mqtt-trigger.conf file
    cJSON *pub_data = cJSON_Parse(topic_value);
    if (pub_data == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
            printf("Error before: %s\n", error_ptr);
        cJSON_Delete(pub_data);
        free(topic_name);
        free(topic_value);
        return;//invalid json data
    }

    cJSON *idx   = cJSON_GetObjectItemCaseSensitive(pub_data, "idx");
    if ( !cJSON_IsNumber(idx) )// || !cJSON_IsString(key)|| !cJSON_IsString(value) || !cJSON_IsString(trig) )
    {
        printf("Error in json-data of idx field(invalid data type)\n");
        cJSON_Delete(pub_data);
        free(topic_name);
        free(topic_value);
        return;
    }
    for (int i = 0; i < TrigEntryList.size(); i++)
    {
        if(TrigEntryList[i].Idx == idx->valueint)
        {
            cJSON *value   = cJSON_GetObjectItemCaseSensitive(pub_data, TrigEntryList[i].Key.c_str());
            if ( !cJSON_IsString(value) )
            {
                printf("Error in json-data of %s field(invalid data type)\n",TrigEntryList[i].Key.c_str());
                cJSON_Delete(pub_data);
                free(topic_name);
                free(topic_value);
                return;
            }
            if((strcmp(value->valuestring,TrigEntryList[i].Value.c_str()) == 0) || (strcmp(value->valuestring,"*") == 0) )
            {
                std::string outstr = TrigEntryList[i].Out;
                std::string custstr = "";
                if(TrigEntryList[i].Custm != "")
                {
                    cJSON *cust   = cJSON_GetObjectItemCaseSensitive(pub_data, TrigEntryList[i].Custm.c_str());
                    if ( cJSON_IsString(cust) )
                    {
                            custstr = cust->valuestring;
                            char cmdstr[1024];
                            //store the custom-value in a /tmp/ file mentioned in json config
                            sprintf(cmdstr,"echo \"%s\" > %s",custstr.c_str(),outstr.c_str());
                            system(cmdstr);
                            system(TrigEntryList[i].Trig.c_str());//trigger the related script or command
                    }
                }
                else
                    system(TrigEntryList[i].Trig.c_str());//trigger the related script or command
            }
        }
    }
    free(topic_name);
    free(topic_value);
}

void* client_refresher(void* client)
{
    while(1)
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}

//check if this is a valid file in the filesystem(returns 0 on success)
int IsValidFile(const char* filepath)
{
        struct stat buffer;
        if(stat(filepath,&buffer)!=0)
                return -1;
        if(buffer.st_mode & S_IFREG)
                return 0;
        return -1;//it could be a directory
}

//int parseJsonConfig(const char* conffile)
int parseJsonConfig(const char* conffile,std::deque<TrigEntry> &TriggerList)
{
    struct stat file_status;
    if (stat(conffile, &file_status) < 0)
        return 0;//unable to get filestats for size

    char* filebuffer = (char*) malloc(file_status.st_size + 1);
    FILE *fp;
    fp = fopen(conffile,"rb");
    size_t bytes_read = fread(filebuffer, 1, file_status.st_size, fp);
    if(bytes_read != file_status.st_size)
    {
        free(filebuffer);
        return -1;
    }

    cJSON *file_data = cJSON_Parse(filebuffer);
    if (file_data == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
            printf("Error before: %s\n", error_ptr);
        cJSON_Delete(file_data);
        free(filebuffer);
        return -1;//invalid json data
    }

    const cJSON *entries = NULL;
    const cJSON *entry = NULL;
    entries = cJSON_GetObjectItemCaseSensitive(file_data, "triggers");
    cJSON_ArrayForEach(entry, entries)
    {
        cJSON *idx   = cJSON_GetObjectItemCaseSensitive(entry, "idx");
        cJSON *key   = cJSON_GetObjectItemCaseSensitive(entry, "key");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(entry, "value");
        cJSON *trig  = cJSON_GetObjectItemCaseSensitive(entry, "trig");
        cJSON *keyout= cJSON_GetObjectItemCaseSensitive(entry, "keyoutput");
        cJSON *custmkey= cJSON_GetObjectItemCaseSensitive(entry, "customkey");
        if ( !cJSON_IsNumber(idx) || !cJSON_IsString(key)|| !cJSON_IsString(value) || !cJSON_IsString(trig) )
        {
            printf("Error in json entry(invalid data type)\n");
            cJSON_Delete(file_data);
            free(filebuffer);
            return -1;
        }
        else
        {
            std::string keyoutstr="";
            std::string custmstr="";
            if(cJSON_IsString(keyout))
                keyoutstr=keyout->valuestring;
            if(cJSON_IsString(custmkey))
                custmstr=custmkey->valuestring;
            TriggerList.push_back(TrigEntry(idx->valueint,key->valuestring,value->valuestring,trig->valuestring,keyoutstr,custmstr));
        }
    }
    cJSON_Delete(file_data);
    free(filebuffer);
    return 0;
}
