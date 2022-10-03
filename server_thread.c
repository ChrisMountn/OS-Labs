#include "request.h"
#include "server_thread.h"
#include "common.h"

//function prototypes
struct hashItem *cache_lookup(struct file_data *data);
struct hashItem* cache_insert(struct file_data *data);
int cache_evict(int bytesToEvict);
struct hashtable * hashtable_init(int max_cache_size);
size_t stringLength(char* source);
static size_t getHash(char* source);
struct hashItem* InsertIntoHashtable(struct file_data *data, int *wasFound);
void hashtable_destroy(struct hashtable *hashtable);
void AppendLRU(struct hashItem *hashItem);
void UpdateLRU(char *file_name);
struct hashItem* RemoveEndOfLRU();
void DestroyLRU();
void printLRU();
void printHashTable();

void server_receive(struct server *sv);

//struct declarations

//Hashtable item/entry
struct hashItem{
	int numThreadsAccessing;
	struct file_data *data;
	char* key;
}; 

//Hashtable itself
struct hashtable {
	struct hashItem* items;
	unsigned long maxLength;
	unsigned long numItems;
	unsigned long bytesFilled;
	unsigned long maxBytes;
};

//listnode for LRU
struct listNode {
	struct hashItem* hashItem;
	struct listNode* nextNode;
	struct listNode* prevNode;
};

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
};

//Global vars for lab 4
int *maxRequestPointer;	//Global pointer that points to the array of max_requests = buffer
pthread_t *threads;
int in = 0;
int out = 0;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t full = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int global_n;
int numThreads;

//Global variables for lab 5
struct hashtable *cache;
pthread_mutex_t tableLock = PTHREAD_MUTEX_INITIALIZER;
struct listNode *LRUHead = NULL;
struct listNode *LRUTail = NULL;
unsigned long int maxCacheSize;

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
	return;
}

static void
do_server_request(struct server *sv, int connfd)
{	
	if(maxCacheSize == 0){
		int ret;
		struct request *rq;
		struct file_data *data;

		data = file_data_init();

		rq = request_init(connfd, data);
		if (!rq) {
			file_data_free(data);
			return;
		}

		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto outt;
		}
		/* send file to client */
		
		request_sendfile(rq);

	outt:
		request_destroy(rq);
		file_data_free(data);
		return;

	}
	//SYS(pthread_mutex_lock(&tableLock));
	//printLRU();
	//SYS(pthread_mutex_unlock(&tableLock));

	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}

	//CHECK IF FILENAME IN HASHTABLE. IF IT IS, get the file directly from there to send to client.
	//Make a request by putting the connfd in the fd, and the loaded data in the file

	struct hashItem* cacheData = cache_lookup(data);

	if(cacheData != NULL){
		request_set_data(rq, cacheData->data);
		request_sendfile(rq);
		SYS(pthread_mutex_lock(&tableLock));
		cacheData->numThreadsAccessing -= 1;
		SYS(pthread_mutex_unlock(&tableLock));
		request_destroy(rq);
		return;
	}
	else{
		/* read file, 
		* fills data->file_buf with the file contents,
		* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */

		//ALSO save the new file in the hashtable. Evict if necessary.
		cacheData = cache_insert(data);
		
		request_sendfile(rq);

		if(cacheData != NULL){
			SYS(pthread_mutex_lock(&tableLock));
			cacheData->numThreadsAccessing -= 1;
			SYS(pthread_mutex_unlock(&tableLock));
			}
		}
out:
	request_destroy(rq);

	//only want to do this if cache doesn't work
	if(cacheData == NULL){
		file_data_free(data);
	}
	return;
}

/* entry point functions */

struct server *server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {

		global_n = max_requests + 1;
		numThreads = nr_threads;

		/* Lab 4: create queue of max_request size when max_requests > 0 */
		if(max_requests > 0){
			maxRequestPointer = malloc(global_n*sizeof(int));
		}
			
		/* Lab 4: create worker threads when nr_threads > 0 */
		threads = Malloc(sizeof(pthread_t)*nr_threads);

		for(int i = 0; i<nr_threads; i++){
			SYS(pthread_create(&threads[i], NULL, (void *(*)(void *))server_receive, (void *)sv));	//initialise pthread with threadID and starter function server request.
		}
	}

	/* Lab 5: init server cache and limit its size to max_cache_size */
	maxCacheSize = max_cache_size;
	if(maxCacheSize != 0){
		cache = hashtable_init(max_cache_size);
	}

	//printf("Server initialized\n");
	return sv;
}

void server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		//send from lecture slides
		SYS(pthread_mutex_lock(&lock));
		while((in - out + global_n)%global_n == global_n - 1){
			SYS(pthread_cond_wait(&full, &lock));
		}
		maxRequestPointer[in] = connfd;
		in = (in + 1) % global_n;
		SYS(pthread_cond_signal(&empty));
		SYS(pthread_mutex_unlock(&lock));
	}
	return;
}

void server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	SYS(pthread_cond_broadcast(&empty));
	for(pthread_t i = 0; i < (pthread_t) numThreads; i++){
		SYS(pthread_join(threads[i], NULL));
	}
	/* make sure to free any allocated resources */
	free(maxRequestPointer);
	free(threads);
	free(sv);
	if(maxCacheSize != 0){
		hashtable_destroy(cache);
		DestroyLRU();
	}
	return;
}

void server_receive(struct server *sv)
{
	//each thread receives the info from the server works on the data.
	//receive from lecture slides
	while(sv->exiting == 0){
		SYS(pthread_mutex_lock(&lock));
		while(in == out){
			SYS(pthread_cond_wait(&empty, &lock));
			if(sv->exiting == 1){
				SYS(pthread_mutex_unlock(&lock));
				return;
			}
		}
		int elem = maxRequestPointer[out];
		out = (out + 1) % global_n;
		SYS(pthread_cond_signal(&full));
		SYS(pthread_mutex_unlock(&lock));
		do_server_request(sv, elem);
	}
	return;
}

struct hashItem *cache_lookup(struct file_data *data){
	SYS(pthread_mutex_lock(&tableLock));
	//printf("LOOKUP: %s\n", data->file_name);
	unsigned long hashValue = getHash(data->file_name);
	unsigned long lookPoint = hashValue % cache->maxLength;

	int complete = 0;
	int iterations = 0;
	while(complete == 0){
		if(cache->items[lookPoint].data == NULL){
			SYS(pthread_mutex_unlock(&tableLock));
			//printf("File not in cache\n");
			return NULL;
		}
		else if(strcmp(cache->items[lookPoint].key, data->file_name) == 0){
			//printf("File in cache\n");
			//increment numthreads using file 
			cache->items[lookPoint].numThreadsAccessing += 1;
			//Update LRU to and move the file to most recently accessed
			UpdateLRU(data->file_name);
			//return file data
			SYS(pthread_mutex_unlock(&tableLock));
			////printf("Looked for %s, file was in cache\n", data->file_name);
			//printLRU();
			return &cache->items[lookPoint];
		}
		else{
			lookPoint = (lookPoint + iterations*iterations) % cache->maxLength;
			complete = 0;
			iterations ++;
		}
	}
	//printf("You really should not get here");
	SYS(pthread_mutex_unlock(&tableLock));
	return NULL;
}

struct hashItem* cache_insert(struct file_data *data){
	SYS(pthread_mutex_lock(&tableLock));
	//printf("INSERT: %s\n", data->file_name);

	//check if file is bigger than entire cache
	if(cache->maxBytes < data->file_size){
		//process the file no need to mess w/ tha cache
		//printf("File %s is too large to put in cache\n", data->file_name);
		SYS(pthread_mutex_unlock(&tableLock));
		return NULL;
	}

	int evictedSuccess = 1;

	//Check if sufficient space. Evict space if not.
	if(cache->bytesFilled + data->file_size > cache->maxBytes){
		//printf("Evicting some mf's\n");
		evictedSuccess = cache_evict(cache->bytesFilled + data->file_size - cache->maxBytes);
	}

	if(evictedSuccess == 0){
		SYS(pthread_mutex_unlock(&tableLock));
		return NULL;
	}
	////printf("inserting file %s into cache table\n", data->file_name);
	int tempVar = 0;
	int *wasFound = &tempVar;
	struct hashItem *newHashItem = InsertIntoHashtable(data, wasFound);

	//insert into LRU list
	if(*wasFound == 0){
		AppendLRU(newHashItem);
	}

	//printf("Added %s to LRU and cache\n", data->file_name);
	//printLRU();

	SYS(pthread_mutex_unlock(&tableLock));
	return newHashItem;
}

int cache_evict(int bytesToEvict){
	//printf("EVICT: %d bytes\n", bytesToEvict);
	int bytesRemoved;

	//Evict files at the end of the LRU cache until enough space has been evicted such that you can fit the new file.
	while(bytesToEvict > 0){
		struct hashItem *currentHashItem = RemoveEndOfLRU();
		if(currentHashItem == NULL){
			//No item in LRU that isn't being used.
			//printf("Eviction failed, returning 0\n");
			return 0;
		}

		//printf("%d bytes removed for %s\n", currentHashItem->data->file_size, currentHashItem->data->file_name);
		bytesRemoved = currentHashItem->data->file_size;

		free(currentHashItem->data->file_buf);
		free(currentHashItem->data->file_name);
		free(currentHashItem->data);
		free(currentHashItem->key);
		cache->numItems--;

		//leave the hashtable entry with the null value in the table as a tombstone
		currentHashItem->data = NULL;
		currentHashItem->key = NULL;
		currentHashItem->numThreadsAccessing = 0;

		bytesToEvict -= bytesRemoved;
		cache->bytesFilled -= bytesRemoved;
		//printf("bytesToEvict now: %d\n", bytesToEvict);
	}
	return 1;
}

/*
Hashtable Code
*/

struct hashtable * hashtable_init(int max_cache_size)
{
	struct hashtable *hashtable;

	hashtable = (struct hashtable *)malloc(sizeof(struct hashtable));
	assert(hashtable);

	//divide max cache size by average file size to find number of entries needed. Multiply by 2 for 0.5 factor.
	//long int tableLength = max_cache_size / 12228;
	//long int tableLength *= 2;

	long int tableLength = max_cache_size;

	hashtable->numItems = 0;
	hashtable->maxLength = tableLength;
	hashtable->items = calloc(tableLength + 1, sizeof(struct hashItem));
	hashtable->bytesFilled = 0;
	hashtable->maxBytes = max_cache_size;

	//assign it to global variable. 
	return hashtable;
}

//returns length of string
size_t stringLength(char* source)
{
    if(source == NULL) { return 0; }

    size_t length = 0;
    while(*source != '\0') {
        length++;
        source++;
    }
    return length;  
}

//returns hash of string source
static size_t getHash(char* source)
{
    size_t length = stringLength(source);
    size_t hash = 0;
    for(size_t i = 0; i < length; i++) {
        char c = source[i];
        int a = c - '0';
        hash = (hash * 10) + a;     
    }

    return hash;
}

struct hashItem* InsertIntoHashtable(struct file_data *data, int *wasFound){
	unsigned long hashValue = getHash(data->file_name);
	unsigned long insertionPoint = hashValue % cache->maxLength;

	char *word;

	int complete = 0;
	int iterations = 0;
	while(complete == 0){
		if(iterations > cache->maxLength){
			//printf("Cannot insert into table. cache is full.\n");
			*wasFound = 1;
			return NULL;
		}
		else if(cache->items[insertionPoint].key == NULL){
			word = strdup(data->file_name);
			struct hashItem * hashItem = &cache->items[insertionPoint];
			hashItem->key = (char*)word;
			hashItem->data = data;
			hashItem->numThreadsAccessing = 1;
			cache->numItems++;
			cache->bytesFilled += data->file_size;
			//printf("Successfully inserted file %s into cache table\n", data->file_name);
			complete = 1;
			*wasFound = 0;
			return hashItem;
		}
		else if(strcmp(cache->items[insertionPoint].key, data->file_name) == 0){
			//printf("File already in table while inserting\n");
			cache->items[insertionPoint].numThreadsAccessing += 1;
			*wasFound = 1;
			return &cache->items[insertionPoint];
		}
		else{
			insertionPoint = (insertionPoint + iterations*iterations) % cache->maxLength;
			complete = 0;
			iterations ++;
		}
	}
	return NULL;
}

//destroys hashtable
void hashtable_destroy(struct hashtable *hashtable)
{	
	for(unsigned long i; i < hashtable->maxLength; i++){
		if(hashtable->items[i].key != NULL){
			//free key, file_buf, file_name, and data from each items struct.
			free((void*)hashtable->items[i].key);
			free((void*)hashtable->items[i].data->file_buf);
			free((void*)hashtable->items[i].data->file_name);
			free((void*)hashtable->items[i].data);
		}
	}
	free(hashtable->items);
	free(hashtable);
	return;
}

/*
LRU linked list Code
*/

/*appends to front of LRU*/
void AppendLRU(struct hashItem *hashItem){
	//printf("APPEND: Appending LRU with %s\n", hashItem->data->file_name);
	struct listNode *insertNode = malloc(sizeof(struct listNode));
	if(LRUHead == NULL && LRUTail == NULL){
		//Runs if LRU is empty.
		//printf("LRU is empty\n");
		insertNode->hashItem = hashItem;
		insertNode->nextNode = NULL;
		insertNode->prevNode = NULL;
		LRUHead = insertNode;
		LRUTail = insertNode;
		return;
	}
	//Runs if LRU is not empty
	insertNode->hashItem = hashItem;
	insertNode->nextNode = LRUHead;
	insertNode->prevNode = NULL;
	LRUHead->prevNode = insertNode;
	LRUHead = insertNode;
	return;
}

/*moves to front of LRU*/
void UpdateLRU(char *file_name){
	//printf("UPDATE LRU: \n");
	struct listNode *tempNode = LRUHead;
	//loop until tempnode filename is target filename.
	//printf("Target File: %s \n", file_name);
	while(strcmp(tempNode->hashItem->data->file_name, file_name) != 0){
		//printf("Current LRU File: %s\n", tempNode->hashItem->data->file_name);
		tempNode = tempNode->nextNode;
		if(tempNode == NULL){
			//printf("reached the end of LRU: %s was not in LRU during update\n", file_name);
			return;
		}
	}
	//If loop exited, our node has been found.
	//printf("%s updated in LRU\n", file_name);
	if(tempNode == LRUHead){
		return;
	}
	else if(tempNode == LRUTail){
		tempNode->prevNode->nextNode = tempNode->nextNode;
		LRUTail = tempNode->prevNode;
		tempNode->prevNode = NULL;
		tempNode->nextNode = LRUHead;
		LRUHead->prevNode = tempNode;
		LRUHead = tempNode;
		return;
	}
	else{
		tempNode->prevNode->nextNode = tempNode->nextNode;
		tempNode->nextNode->prevNode = tempNode->prevNode;
		tempNode->prevNode = NULL;
		tempNode->nextNode = LRUHead;
		LRUHead->prevNode = tempNode;
		LRUHead = tempNode;
	}	
}


/*Gets rid of end of LRU if no threads accessing. Moves backwards along LRU to find first mf with no threads accessing.*/
struct hashItem* RemoveEndOfLRU(){
	//printf("REMOVE FROM LRU\n");
	//printLRU();
	struct listNode *tempNode = LRUTail;
	//printf("file %s has %d users\n", tempNode->hashItem->data->file_name, tempNode->hashItem->numThreadsAccessing);
	while(tempNode->hashItem->numThreadsAccessing > 0){
		tempNode = tempNode->prevNode;
		if(tempNode == NULL){
			//printf("No files available for removal\n");
			return NULL;
		}
		//printf("file %s has %d users\n", tempNode->hashItem->data->file_name, tempNode->hashItem->numThreadsAccessing);
	}
	//printf("Removing file %s with no threads using\n", tempNode->hashItem->data->file_name);

	struct hashItem *deletedData = tempNode->hashItem;

	if(tempNode != LRUHead && tempNode != LRUTail){
		tempNode->prevNode->nextNode = tempNode->nextNode;
		tempNode->nextNode->prevNode = tempNode->prevNode;
	}
	else if (tempNode == LRUTail && tempNode == LRUHead){
		LRUTail = NULL;
		LRUHead = NULL;
	}
	else if(LRUTail == tempNode){
		tempNode->prevNode->nextNode = tempNode->nextNode;
		LRUTail = tempNode->prevNode;
	}
	else if(LRUHead == tempNode){
		tempNode->nextNode->prevNode = tempNode->prevNode;
		LRUHead = tempNode->nextNode;
	}
	free(tempNode);
	//printLRU();
	return deletedData;
}

//get that shit outta hyeah 
void DestroyLRU(){
	struct listNode *tempNode = LRUHead;
	struct listNode *freeNode = NULL;
	while(tempNode->nextNode != NULL){
		freeNode = tempNode;
		tempNode = tempNode->nextNode;
		free(freeNode);
	}
	free(tempNode);
}

////printing for debugging purposes.
void printLRU(){
	printf("\nLRU:\n");
	struct listNode * currentNode = LRUHead;
	while(currentNode != NULL){
		printf("Key: %s Num threads accessing: %d Size: %d -> \n", currentNode->hashItem->data->file_name, currentNode->hashItem->numThreadsAccessing, currentNode->hashItem->data->file_size);
		currentNode = currentNode->nextNode;
	}
	printf("Total cache contains %ld out of %ld bytes\n", cache->bytesFilled, cache->maxBytes);
	printHashTable();
}

void printHashTable(){
	printf("\nCache:\n");
	for(unsigned long i = 0; i < cache->maxLength; i++){
		if(cache->items[i].key != NULL){
			printf("Key: %s Num threads accessing: %d Size: %d \n", cache->items[i].key, cache->items[i].numThreadsAccessing, cache->items[i].data->file_size);
		}
	}
}