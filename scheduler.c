#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include "util/so_scheduler.h"

thread* find_thread(pthread_t tid)
{
    thread* temp = scheduler.head;

    while(temp != NULL) {
        if (pthread_equal(temp->tid, tid))
            break;
        temp = temp->next;
    }

    return temp;
}

void print_thread(thread* aux)
{
    printf("tid = %li, state = %d, prio = %d, quantum = %d\n\n", aux->tid, aux->state, aux->priority, aux->time_quantum);
}

void print_thread_queue()
{
    thread* aux = scheduler.head;

    while(aux != NULL) {
        printf("{ tid = %li, state = %d, prio = %d, quantum = %d }, ", aux->tid, aux->state, aux->priority, aux->time_quantum);
        aux = aux->next;
    }

    printf("\n\n\n");
}

void quantum_expired(thread* t)
{
    pthread_mutex_lock(&scheduler.lock);
    thread* aux = scheduler.head;

    t->time_quantum = scheduler.time_quantum;

    // if i'm the only thread i have nothing to reorganize.
    if(aux->next == NULL) {
        pthread_mutex_unlock(&scheduler.lock);
        return;
    }

    if(aux == t) {
            scheduler.head = aux->next;
    } else {

        while (aux->next != NULL){

            if (aux->next == t) {
                aux->next = t->next;
                break;
            }

            aux = aux->next;
        }
    }

    t->next = NULL;

    //reinserting node according to priority
    if (scheduler.head == NULL) {
        scheduler.head = t;
        scheduler.head->next = NULL;
    } else {

        thread* aux = scheduler.head;

        if(aux->next == NULL) { // only one thread in the list
            if (aux->priority < t->priority) {
                t->next = aux;
                scheduler.head = t;
            }
            else {
                aux->next = t;
            }
        } else {

            thread* tmp = scheduler.head;

            while (aux != NULL && aux->priority >= t->priority) {
                tmp = aux;
                aux = aux->next;
            }
            // i have to insert the node just after tmp
            if (aux == scheduler.head) {
                t->next = tmp;
                scheduler.head = t;
            } else {
                tmp->next = t;
                t->next = aux;
            }
        }
    }

    pthread_mutex_unlock(&scheduler.lock);

}

int so_init(unsigned int time_quantum, unsigned int io)
{

    if (io > SO_MAX_NUM_EVENTS)
        return -1;

    if (time_quantum < 1)
        return -1;

    if (scheduler.isInitialized == 1)
        return -1;

    scheduler.time_quantum = time_quantum;
    scheduler.events_max_count = io;
    scheduler.head = NULL;
    scheduler.isInitialized = 1;
    scheduler.threads_current_size = 0;

    pthread_mutex_init(&scheduler.lock, NULL);
    pthread_mutex_init(&scheduler.finished_lock, NULL);
    pthread_mutex_init(&scheduler.condition_lock, NULL);
    pthread_cond_init(&scheduler.cond_all_threads_finished, NULL);

    return 0;
}

thread* check_scheduler()
{

    pthread_mutex_lock(&scheduler.lock);

    thread* t = scheduler.head;
    thread* lastThread;

    while (t != NULL) {

        if (t->state != WAITING && t->state != TERMINATED && t->time_quantum > 0) {
                pthread_mutex_unlock(&scheduler.lock);
                return t;
        }
        lastThread = t;
        t=t->next;
    }

    pthread_mutex_unlock(&scheduler.lock);

    if(scheduler.threads_current_size == 1 && scheduler.head->time_quantum == 0)
        return scheduler.head;

    if(lastThread->time_quantum == 0)
        return lastThread;

    /*no threads to schedule*/
    return NULL;
}

int all_threads_finished()
{
    thread* t = scheduler.head;

    while(t != NULL)
    {
        if(t->state != TERMINATED)
            return 0;

        t = t->next;
    }

    return 1;
}

void *strart_thread(void *param)
{
    pthread_t tid = pthread_self();
    thread* aux = find_thread(tid);
    thread* toRun;

    pthread_mutex_lock(&scheduler.condition_lock);

    if(aux->parent != NULL)
        pthread_cond_signal(&aux->parent->cond_start_thread_called); // wake up parent

    while ((toRun = check_scheduler()) != aux) {
        if(!pthread_equal(toRun->tid, tid)) {
            pthread_cond_signal(&toRun->cond_ready_running_state);
            pthread_cond_wait(&aux->cond_ready_running_state, &scheduler.condition_lock);
        }
    }

    pthread_mutex_unlock(&scheduler.condition_lock);

    function_param *f = (function_param*)param;

    unsigned int priority = f->priority;

    so_handler* func = (so_handler*)f->function;

    func(priority);

    aux->state = TERMINATED;

    /*thread finished executing wake up other threads*/

    toRun = check_scheduler();

    if(toRun != NULL)
        pthread_cond_signal(&toRun->cond_ready_running_state);

    if(all_threads_finished())
        pthread_cond_signal(&scheduler.cond_all_threads_finished);

    return NULL;
}

tid_t so_fork(so_handler *func, unsigned int priority)
{
    thread *newThread, *currentThread;
    pthread_t pthread;
    function_param* f = (function_param*)calloc(1, sizeof(function_param));
    currentThread = find_thread(pthread_self());

    if (priority < 0
        || priority > SO_MAX_PRIO
    ) {
        return INVALID_TID;
    }

    if (func == NULL) {
        return INVALID_TID;
    }

    f->priority = priority;
    f->function = func;

    scheduler.threads_current_size = scheduler.threads_current_size + 1;

    newThread = (thread *)calloc(1, sizeof(thread));
    newThread->priority = priority;
    newThread->state = READY;
    newThread->receivedSignal = 0;
    newThread->next = NULL;
    newThread->time_quantum = scheduler.time_quantum;
    newThread->parent = currentThread;
    pthread_cond_init(&newThread->cond_ready_running_state, NULL);
    pthread_cond_init(&newThread->cond_event, NULL);
    pthread_cond_init(&newThread->cond_start_thread_called, NULL);
    pthread_mutex_init(&newThread->mutex, NULL);

    if (scheduler.head == NULL) {
        scheduler.head = newThread;
    } else {

        thread* aux = scheduler.head;

        if(aux->next == NULL) { // only one thread in the list
            if (aux->priority < priority){
                newThread->next = aux;
                scheduler.head = newThread;
            }
            else {
                aux->next = newThread;
            }
        } else {

            thread* tmp = scheduler.head;

            while(aux != NULL && aux->priority >= priority) {
                tmp = aux;
                aux = aux->next;
            }

            if(aux == scheduler.head) {
                newThread->next = tmp;
                scheduler.head = newThread;
            } else {
                tmp->next = newThread;
                newThread->next = aux;
            }

        }
    }

    if(pthread_create(&pthread, NULL, &strart_thread, f)) {
        return INVALID_TID;
    }

    newThread->tid = pthread;

    //currentThread is null only for the thread who forks the first thread
    if(currentThread != NULL) {

        pthread_mutex_lock(&scheduler.condition_lock);

        pthread_cond_wait(&currentThread->cond_start_thread_called, &scheduler.condition_lock);

        currentThread->time_quantum--;

        thread* toRun;
        while ((toRun = check_scheduler()) != currentThread) {
            if(!pthread_equal(toRun->tid, pthread_self())) {
                if(currentThread->time_quantum <= 0) {    // preempted because of quantum -> reset quantum


                    quantum_expired(currentThread);

                }

                pthread_cond_signal(&toRun->cond_ready_running_state);
                pthread_cond_wait(&currentThread->cond_ready_running_state, &scheduler.condition_lock);
            }
        }
        pthread_mutex_unlock(&scheduler.condition_lock);
    }

    return pthread;
}

int so_wait(unsigned int io)
{
    pthread_t tid = pthread_self();
    thread* currentThread = find_thread(tid);
    thread* toRun;

    if (io < 0)
        return INVALID_TID;

    if (io > SO_MAX_NUM_EVENTS)
        return INVALID_TID;

    thread* t = find_thread(tid);

    t->event = io;
    t->state = WAITING;

    pthread_mutex_lock(&scheduler.condition_lock);

    pthread_cond_wait(&t->cond_event, &scheduler.condition_lock);
    pthread_mutex_unlock(&scheduler.condition_lock);

    pthread_mutex_lock(&scheduler.condition_lock);

    while ((toRun = check_scheduler()) != currentThread) {

        if(!pthread_equal(toRun->tid, tid)) {
            if(currentThread->time_quantum <= 0){    // preempted because of quantum -> reset quantum
                quantum_expired(currentThread);
            }

            pthread_cond_signal(&toRun->cond_ready_running_state);
            pthread_cond_wait(&currentThread->cond_ready_running_state, &scheduler.condition_lock);
        }
    }

    pthread_mutex_unlock(&scheduler.condition_lock);

    return 0;
}

int so_signal(unsigned int io)
{

    pthread_t tid = pthread_self();
    thread* t = scheduler.head;
    thread* currentThread = find_thread(tid);
    thread* toRun;

    while (t != NULL) {
        if (t->event == io) {
            t->state = READY;
            pthread_cond_signal(&t->cond_event);
        }
        t = t->next;
    }

    pthread_mutex_lock(&scheduler.condition_lock);

    while ((toRun = check_scheduler()) != currentThread) {

        if(!pthread_equal(toRun->tid, tid)) {
            if(currentThread->time_quantum <= 0){    // preempted because of quantum -> reset quantum
                quantum_expired(currentThread);
            }

            pthread_cond_signal(&toRun->cond_ready_running_state);
            pthread_cond_wait(&currentThread->cond_ready_running_state, &scheduler.condition_lock);
        }
    }

    pthread_mutex_unlock(&scheduler.condition_lock);
    return 0;
}

void so_exec(void)
{
    pthread_t tid = pthread_self();
    thread* currentThread = find_thread(tid);
    thread* toRun;

    currentThread->time_quantum--;
    pthread_mutex_lock(&scheduler.condition_lock);

    while ((toRun = check_scheduler()) != currentThread) {

        if(!pthread_equal(toRun->tid, tid)) {
            if(currentThread->time_quantum <= 0){    // preempted because of quantum -> reset quantum
                quantum_expired(currentThread);
            }

            pthread_cond_signal(&toRun->cond_ready_running_state);
            pthread_cond_wait(&currentThread->cond_ready_running_state, &scheduler.condition_lock);
        }
    }

    pthread_mutex_unlock(&scheduler.condition_lock);
}

void so_end(void)
{
    thread* t = scheduler.head;

    pthread_mutex_lock(&scheduler.finished_lock);

    //If i have threads in the scheduler wait for them to finish
    if(scheduler.threads_current_size > 0)
        pthread_cond_wait(&scheduler.cond_all_threads_finished, &scheduler.finished_lock);

    while(t != NULL) {
        pthread_join(t->tid, NULL);
        t->state = TERMINATED;
        t = t->next;
    }

    scheduler.isInitialized = 0;
}
