#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>

#define MAX_TRAINS 100

// strong semaphore implementation using pthreads since it acts like a queue

typedef struct Node {
    pthread_t thread_id;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    Node* tail;
} Queue;


// Initialize the queue
void initQueue(Queue* queue) {
    queue->head = NULL;
    queue->tail = NULL;
}

// Enqueue operation
void enqueue(Queue* queue, pthread_t thread_id) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->thread_id = thread_id;
    new_node->next = NULL;

    if (queue->tail == NULL) {
        queue->head = new_node;
        queue->tail = new_node;
    } else {
        queue->tail->next = new_node;
        queue->tail = new_node;
    }
}

// Dequeue operation
pthread_t dequeue(Queue* queue) {
    if (queue->head == NULL) {
        return (pthread_t)NULL; // or appropriate error handling
    }

    Node* temp = queue->head;
    pthread_t thread_id = temp->thread_id;
    queue->head = queue->head->next;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    free(temp);
    return thread_id;
}

// Check if the queue is empty
int isQueueEmpty(Queue* queue) {
    return queue->head == NULL;
}

// Destroy the queue
void destroyQueue(Queue* queue) {
    while (!isQueueEmpty(queue)) {
        dequeue(queue);
    }
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    Queue queue;  // Added queue to the semaphore structure
} StrongSemaphore;

// Initialize the semaphore
void initStrongSemaphore(StrongSemaphore *sem, int value) {
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->count = value;
    initQueue(&sem->queue);  // Initialize the queue
}

// Wait (P) operation
void waitStrongSemaphore(StrongSemaphore *sem) {
    pthread_mutex_lock(&sem->mutex);

    if (sem->count > 0) {
        sem->count--;
        pthread_mutex_unlock(&sem->mutex);
        return;
    }

    // Enqueue this thread
    enqueue(&sem->queue, pthread_self());

    while (1) {
        pthread_cond_wait(&sem->cond, &sem->mutex);
        // Check if it's this thread's turn
        if (sem->queue.head != NULL && pthread_equal(sem->queue.head->thread_id, pthread_self())) {
            break;
        }
    }

    sem->count--;
    dequeue(&sem->queue);  // Remove this thread from the queue

    pthread_mutex_unlock(&sem->mutex);
}

// Signal (V) operation
void signalStrongSemaphore(StrongSemaphore *sem) {
    pthread_mutex_lock(&sem->mutex);

    sem->count++;
    if (!isQueueEmpty(&sem->queue)) {
        pthread_cond_signal(&sem->cond);  // Wake up the first thread in the queue
    }

    pthread_mutex_unlock(&sem->mutex);
}

// Destroy the semaphore
void destroyStrongSemaphore(StrongSemaphore *sem) {
    pthread_mutex_destroy(&sem->mutex);
    pthread_cond_destroy(&sem->cond);
    destroyQueue(&sem->queue);  // Clean up the queue
}

enum Section {
    SECTION_AC = 1,
    SECTION_BC = 2,
    SECTION_DE = 3,
    SECTION_DF = 4
};

typedef struct Train {
    int id;
    int timeTunnel; // 2 or 3 if length is 100 length = 2 else length = 3
    int section; // 1 A-B 2 B-C 3 C-D 4 E-D 5 F-D
} Train;

typedef struct thread_list {
    pthread_t thread;
    struct thread_list *next;
} thread_list;

thread_list *threads = NULL;
double p;//probability of train arriving at A
int simulation_time; 
int num_trains = 0; // Total number of trains that arrived
int simulation_complete = 0; // Flag to indicate simulation completion
int waiting_AC = 0;
int waiting_BC = 0;
int waiting_DE = 0;
int waiting_DF = 0;
time_t start_time;
time_t end_time;


StrongSemaphore sem_A;
StrongSemaphore sem_B;
StrongSemaphore sem_E;
StrongSemaphore sem_F;

pthread_mutex_t tunnel_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for accessing tunnel
pthread_mutex_t increment_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for incrementing semaphore

void *generate_train(void *arg);
void *put_in_queue(void *arg);
void *tunnel_control(void *arg);
void *add_new_thread(Train *t);
double getRandom();

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: ./project2 <probability> -s <simulation time>\n");
        return 1;
    }
    p = atof(argv[1]);
    simulation_time = atoi(argv[3]);

    start_time = time(NULL);
    end_time = start_time + simulation_time;

    pthread_t train_generator;
    pthread_t tunnel_controller;
    pthread_create(&train_generator, NULL, generate_train, NULL);
    pthread_create(&tunnel_controller, NULL, tunnel_control, NULL);

    //strong semaphore initialization
    initStrongSemaphore(&sem_A, 0);
    initStrongSemaphore(&sem_B, 0);
    initStrongSemaphore(&sem_E, 0);
    initStrongSemaphore(&sem_F, 0);

    while (time(NULL) < end_time) { 
        sleep(1);
    }
    
    pthread_kill(train_generator, SIGTERM);
    pthread_kill(tunnel_controller, SIGTERM);

    printf("Simulation complete\n");
    printf("Total number of trains that arrived: %d\n", num_trains);

    return 0;

}
double getRandom(){
    return (double)rand()/(double)(RAND_MAX);
}
void *generate_train(void *arg) { // A-C
    
    while(time(NULL) < end_time){
    
        if(getRandom()< p){
            Train* t = (Train*) malloc(sizeof(Train));
            if (t == NULL) {
                fprintf(stderr, "Failed to allocate memory for train\n");
                exit(1);
            }
            t->id = num_trains++;
            t->timeTunnel = rand() % 100 < 70 ? 2 : 3;
            t->section = SECTION_AC; // Start at A, go through AC
            add_new_thread(t);
        }
        if(getRandom() < 1-p){
            Train* t = (Train*) malloc(sizeof(Train));
            if (t == NULL) {
                fprintf(stderr, "Failed to allocate memory for train\n");
                exit(1);
            }
            t->id = num_trains++;
            t->timeTunnel = rand() % 100 < 70 ? 2 : 3;
            t->section = SECTION_BC; // Start at B, go through BC
            add_new_thread(t);
        }
        if(getRandom() < p){
            Train* t = (Train*) malloc(sizeof(Train));
            if (t == NULL) {
                fprintf(stderr, "Failed to allocate memory for train\n");
                exit(1);
            }
            t->id = num_trains++;
            t->timeTunnel = rand() % 100 < 70 ? 2 : 3;
            t->section = SECTION_DE; // Start at E, go through DE
            add_new_thread(t);
        }
        if(getRandom() < p){
            Train* t = (Train*) malloc(sizeof(Train));
            if (t == NULL) {
                fprintf(stderr, "Failed to allocate memory for train\n");
                exit(1);
            }
            t->id = num_trains++;
            t->timeTunnel = rand() % 100 < 70 ? 2 : 3;
            t->section = SECTION_DF; // Start at F, go through DF
            add_new_thread(t);
        }
        sleep(1);
        }
    pthread_exit(NULL);
    
}

void *add_new_thread(Train *t){
    pthread_t new_train;
    thread_list *new_thread = (thread_list *) malloc(sizeof(thread_list));
    if (new_thread == NULL) {
        fprintf(stderr, "Failed to allocate memory for thread\n");
        exit(1);
    }
    new_thread->thread = new_train;
    new_thread->next = NULL;
    if(threads == NULL){
        threads = new_thread;
    }
    else{
        thread_list *current = threads;
        while(current->next != NULL){
            current = current->next;
        }
        current->next = new_thread;
    }
    pthread_create(&new_train, NULL, put_in_queue, t);
}

void *put_in_queue(void *arg){
    Train *train = (Train *) arg;
    printf("Train ID: %d waiting to enter tunnel from section %d\n", train->id, train->section);
    if(train->section==SECTION_AC){
        pthread_mutex_lock(&increment_mutex);
        waiting_AC++;
        pthread_mutex_unlock(&increment_mutex);
    }
    else if(train->section==SECTION_BC){
        pthread_mutex_lock(&increment_mutex);
        waiting_BC++;
        pthread_mutex_unlock(&increment_mutex);
    }
    else if(train->section==SECTION_DE){
        pthread_mutex_lock(&increment_mutex);
        waiting_DE++;
        pthread_mutex_unlock(&increment_mutex);
    }
    else if(train->section==SECTION_DF){
        pthread_mutex_lock(&increment_mutex);
        waiting_DF++;
        pthread_mutex_unlock(&increment_mutex);
    }
    sleep(1);
    
    if(train->section==SECTION_AC){
        waitStrongSemaphore(&sem_A);
    }
    else if(train->section==SECTION_BC){
        waitStrongSemaphore(&sem_B);
    }
    else if(train->section==SECTION_DE){
        waitStrongSemaphore(&sem_E);
    }
    else if(train->section==SECTION_DF){
        waitStrongSemaphore(&sem_F);
    }
    pthread_mutex_lock(&tunnel_mutex);
    printf("Train ID: %d entered tunnel from section %d\n", train->id, train->section);

    sleep(train->timeTunnel);

    if(train->section==SECTION_AC){
        waiting_AC--;
    }
    else if(train->section==SECTION_BC){
        waiting_BC--;
    }
    else if(train->section==SECTION_DE){
        waiting_DE--;
    }
    else if(train->section==SECTION_DF){
        waiting_DF--;
    }
    pthread_mutex_unlock(&tunnel_mutex);
    printf("Train ID: %d exited tunnel\n", train->id);
}

void *tunnel_control(void *arg){
    
    while(time(NULL) < end_time){
        pthread_mutex_lock(&tunnel_mutex); 
        pthread_mutex_unlock(&tunnel_mutex);
    
                
        if(waiting_AC + waiting_BC + waiting_DE + waiting_DF > MAX_TRAINS){
            printf("Too many trains waiting\n");
            pthread_exit(NULL);
        }
        else if(waiting_AC>0 || waiting_BC>0 || waiting_DE>0 || waiting_DF>0){
            int max_waiting = 0;
            
            if(waiting_AC>max_waiting){
                max_waiting = waiting_AC;
            }
            if(waiting_BC>max_waiting){
                max_waiting = waiting_BC;
            }
            if(waiting_DE>max_waiting){
                max_waiting = waiting_DE;
            }
            if(waiting_DF>max_waiting){
                max_waiting = waiting_DF;
            }
            printf("Max waiting: %d\n", max_waiting);
            printf("Waiting AC: %d\n", waiting_AC);
            printf("Waiting BC: %d\n", waiting_BC);
            printf("Waiting DE: %d\n", waiting_DE);
            printf("Waiting DF: %d\n", waiting_DF);

            if(max_waiting==waiting_AC){
                signalStrongSemaphore(&sem_A);
            }
            else if(max_waiting==waiting_BC){
                signalStrongSemaphore(&sem_B);
            }
            else if(max_waiting==waiting_DE){
                signalStrongSemaphore(&sem_E);
            }
            else if(max_waiting==waiting_DF){
                signalStrongSemaphore(&sem_F);
            }
            sleep(1);
        }
    
    }
    pthread_exit(NULL); 
}