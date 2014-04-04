#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <libcouchbase/couchbase.h>

#define STARTING_REST_PORT 9000
#define STARTING_MEMD_PORT 12000

#define KEY_SIZE 1024

//Cluster node struct 
typedef struct node_st {
    int rest_port;
    int memd_port;
} *node_st;

//Node and bucket config 
typedef struct config_st {
    char *host;
    char *bucket;
    int cccp_enabled;
    size_t workload;
    size_t data_size;
} *config_st;

//Uses mutex lock
typedef struct error_st {
    const char *str_error;
    char op[1024];
    struct error_st *next;
} *error_st;

typedef struct stats_st{
    int passed;
    int failed;
} *stats_st;


pthread_mutex_t mutex;
config_st globalconfig;
stats_st stats;

void
print_stats(){
    fprintf(stdout, "Passed: %d \n", stats->passed);
    fprintf(stdout, "Failed: %d \n", stats->failed);
    return;
}

void
error_callback(lcb_t instance, lcb_error_t error, const char *errinfo) {
        fprintf(stderr, "error %s \n", lcb_strerror(instance, error));
        exit(1);
}

void
get_callback(lcb_t instance, 
        const void *cookie, 
        lcb_error_t error,
        const lcb_get_resp_t *item) {

    pthread_mutex_lock(&mutex);
    if (error == LCB_SUCCESS) {
        stats->passed += 1;

    }
    else {
        stats->failed += 1;
        error_st err = calloc(1, sizeof(err));
        err->str_error = lcb_strerror(instance, error);
        sprintf(err->op, "GET");
    }
    pthread_mutex_unlock(&mutex);
}


void
store_callback(lcb_t instance, 
        const void *cookie, 
        lcb_storage_t operation,
        lcb_error_t error,
        const lcb_store_resp_t *item) {


    pthread_mutex_lock(&mutex);
    if (error == LCB_SUCCESS) {
        stats->passed += 1;
    }
    else {
        stats->failed += 1;
        error_st err = calloc(1, sizeof(err));
        err->str_error = lcb_strerror(instance, error);
        sprintf(err->op, "SET");
    }
    pthread_mutex_unlock(&mutex);
}


static void*
handle(void *data) {

    lcb_t instance;                                                             
    struct lcb_create_st options;
    node_st node = (node_st)data;
    char host[1024];
    lcb_error_t err;

    int chunks = globalconfig->data_size/sizeof(char *);
    int kvcount = globalconfig->workload/2;

    memset(&options, 0, sizeof(options));

    if (globalconfig->cccp_enabled) {
        //Force to use only cccp
        lcb_config_transport_t enabled_transports = LCB_CONFIG_TRANSPORT_CCCP;
        sprintf(host, "%s:%d", globalconfig->host, node->memd_port);
        options.version = 2;
        options.v.v2.mchosts = host; 
        options.v.v2.bucket = globalconfig->bucket;
        options.v.v2.transports = &enabled_transports;
        options.v.v0.user = globalconfig->bucket;
    } else {
        sprintf(host, "%s:%d", globalconfig->host, node->rest_port);
        options.v.v0.host = host; 
        options.v.v0.bucket = globalconfig->bucket;
        options.v.v0.user = globalconfig->bucket;
    }



    if ((err = lcb_create(&instance, &options)) != LCB_SUCCESS) {
        fprintf(stderr, "create error %s", lcb_strerror(instance, err));
        exit(1);
    }


    if ((err = lcb_connect(instance)) != LCB_SUCCESS) {
       fprintf(stderr, "connect error %s", lcb_strerror(instance, err));
        lcb_destroy(instance);
        exit(1);
    }
    lcb_wait(instance);

    (void)lcb_set_get_callback(instance, get_callback);
    (void)lcb_set_store_callback(instance, store_callback);

    while(kvcount--) {
        lcb_store_cmd_t cmd;
        const lcb_store_cmd_t *commands[1];
        char key[1024];
        char *data = (char *)calloc(1024, sizeof(*data));
        memset(data,'a', sizeof(*data) * 1024);

        commands[0] = &cmd;
        memset(&cmd, 0, sizeof(cmd));
        sprintf(key, "%d", kvcount);
        cmd.v.v0.operation = LCB_SET;
        cmd.v.v0.key = key;
        cmd.v.v0.nkey = strlen(key);
        cmd.v.v0.bytes =  data;
        cmd.v.v0.nbytes = 1024 * sizeof(*data);
        err = lcb_store(instance, NULL, 1, commands);
        if (err != LCB_SUCCESS) {
            fprintf(stderr, "Failed to store %s \n", lcb_strerror(NULL, err));
        }
    }

    lcb_wait(instance);
    kvcount = globalconfig->workload/2;

    while(kvcount--) {
        lcb_get_cmd_t cmd;
        char key[1024];
        const lcb_get_cmd_t *commands[1];

        commands[0] = &cmd;
        memset(&cmd, 0, sizeof(cmd));
        sprintf(key, "%d", kvcount); 
        cmd.v.v0.key = key;
        cmd.v.v0.nkey = strlen(key);
        err = lcb_get(instance , NULL, 1, commands);
        if (err != LCB_SUCCESS) {
            fprintf(stderr, "Failed to get %s \n", lcb_strerror(NULL, err));
        }
    }

    lcb_wait(instance);
    lcb_destroy(instance);
    return NULL;
}


// Usage:
// n number of nodes to connect
// b bootstrap option to be if cccp needs to be enabled
// d data set size
// s data size 
int
main (int argc, char **argv)
{

    size_t num_nodes = 0;
    int cccpflag = 1;
    size_t workload = 0;
    size_t data_size = 0;
    int c, i;
    char host[1024];
    int opterr = 0;

    globalconfig = (config_st)calloc(1, sizeof(*globalconfig));

    if (!globalconfig) {
        fprintf(stderr, "PANIC: OOM");
        exit(1);
    }

    memset(globalconfig, 0, sizeof(*globalconfig));

    stats = (stats_st)calloc(1, sizeof(*stats));
    if (!stats) {
        fprintf(stderr, "PANIC: OOM");
        exit(1);
    }

    memset(stats, 0, sizeof(*stats));

    while ((c = getopt(argc, argv, "n:b:d:s")) != -1) {
        switch (c)
        {
            case 'h':
                sprintf(host,"%s", optarg);
                break;
            case 'n':
                num_nodes = atoi(optarg);
                break;
            case 'b':
                cccpflag = atoi(optarg);
                if (cccpflag != 1 || cccpflag != 0) {
                    perror("Illegal value for cccp flag");
                    exit(1);
                }
                else {
                    globalconfig->cccp_enabled = cccpflag;
                }
                break;
            case 'd':
                workload = atoi(optarg);
                break;
            case 's':
                data_size = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unrecognized option");
                break;
        }
    }

    if (!data_size) {
        //Setting the data size to 1Byte
        data_size = 1;
    }

    if(!workload) {
        //Setting the workload to 1000 ops
        workload = 1000;
    }

    if (num_nodes > 0) {
        fprintf(stderr, "Starting up with %zu Number of nodes \n", num_nodes);
        fprintf(stderr, "Starting the RAMP Phase \n");

        pthread_t th[num_nodes];
        globalconfig->workload = workload;
        globalconfig->data_size = 1;
        globalconfig->bucket = "default";
        globalconfig->host = "localhost";

        if (pthread_mutex_init(&mutex, NULL)) {
            perror("Unable to initialize mutex");
            exit(1);
        }

        for (i = 0;i < num_nodes; i++) {
            node_st node = calloc(1, sizeof(node));
            node->rest_port = STARTING_REST_PORT + i; 
            node->memd_port = STARTING_MEMD_PORT + i;

            if(pthread_create(&th[i], NULL, handle, (void *)node) != 0) {
                perror("PANIC: Unable to create pthread. Exiting...");
                exit(1);
            }
        }

        for(i = 0; i < num_nodes; i++) {
            if(pthread_join(th[i],NULL)) {
                perror("PANIC: Unable to join pthread. Exiting...");
                exit(1);
            }
        }
        pthread_mutex_destroy(&mutex);
        //print stats
        print_stats();

    } else {
        fprintf(stderr, "Atleast one node is required");
        exit(1);
    }
    free(globalconfig);
    free(stats);
    return 0;
}
