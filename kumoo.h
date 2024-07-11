#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ADDR_SIZE 16

struct pcb *current;
unsigned short *pdbr;
char *pmem, *swaps;
int pfnum, sfnum;

void ku_dump_pmem(void);
void ku_dump_swap(void);


struct pcb{
	unsigned short pid;
	FILE *fd;
	unsigned short *pgdir;

	/* Add more fields as needed */
	unsigned short segment_start;
	unsigned short segment_size;

	unsigned short pd_index;			//pgdir의 PFN 정보 --> 나중에 ku_proc_exit 함수에서 free 시켜주기 위한 것 
	struct pcb *nextPcb;				//생성된 pcb를 별도의 queue에서 관리하는 것이 아니라 pcb끼리 linked_list 형태로 이어지게 한다. 
};

//physical address space의 freeList를 Linked_List로 구현하기 위해 하나의 Node를 구조체로 선언 
typedef struct FreeNode FreeNode;
struct FreeNode{
	char used;
	char swapable;
	unsigned short pfn;
	FreeNode *nextNode;
};

//Swap space의 freeList를 Linked_List로 구현하기 위해 하나의 Node를 구조체로 선언 
typedef struct SwapNode SwapNode;
struct SwapNode{
	char used;
	unsigned short index;
	unsigned short pid;
	SwapNode *nextNode;
};

//eviction 시킬 page를 FIFO 방식의 Linked_List로 구현하기 위해 하나의 Node를 구조체로 선언 
typedef struct EvictNode EvictNode;
struct EvictNode{
	unsigned short pfn;
	unsigned short *pte;		// eviction될 page의 정보가 있는 pte이다.  만약 해당 PFN이 가르키는 곳이 page가 아니라 page table이라면 이는 pde 정보가 된다.
	EvictNode *nextNode;
	unsigned short pid;
};

//PCB 시작주소
struct pcb *startPcb = NULL;
//physical address space의 freeList 시작주소
FreeNode *freeList;
//Swap space의 freeList 시작주소
SwapNode *swapList;

//Eviction Queue의 시작주소
EvictNode * startEvict = NULL;
//Eviction Queue의 끝주소
EvictNode * endEvict = NULL;

void ku_freelist_init(){

	//physical address space의 freeList 초기화
	freeList = (FreeNode *)malloc(sizeof(FreeNode));
	freeList -> used = 0;
	freeList -> swapable = 1;
	freeList -> pfn = 0;

	FreeNode *currentPcb = freeList;

	for(int i = 1; i < pfnum; i++){
		currentPcb -> nextNode = (FreeNode *)malloc(sizeof(FreeNode));    // 이 메모리 free는 프로그램이 종료될 때 OS가 free 시켜주는 것으로 하자. 우리 프로그램에서 별도로 free 시키지 않을 것이다. 
		currentPcb = currentPcb -> nextNode;

		currentPcb -> used = 0;
		currentPcb -> swapable = 1;
		currentPcb -> pfn = i;
	}


	//swap space의 freeList 초기화 
	swapList = (SwapNode *)malloc(sizeof(SwapNode));
	swapList -> used = 0;
	swapList -> index = 0;
	swapList -> pid = 11;

	SwapNode *currentNodeSwap = swapList;

	for(int i = 1; i < sfnum; i++){
		currentNodeSwap -> nextNode = (SwapNode *)malloc(sizeof(SwapNode));    // 이 메모리 free는 프로그램이 종료될 때 OS가 free 시켜주는 것으로 하자. 우리 프로그램에서 별도로 free 시키지 않을 것이다. 
		currentNodeSwap = currentNodeSwap -> nextNode;

		currentNodeSwap -> used = 0;
		currentNodeSwap -> index = i;
		currentNodeSwap -> pid  = 11;
	}

	// 									<<제대로 초기화 되었는지 test 해보자.>>
	// FreeNode *temp = freeList;e
	// for(int i = 0; i < pfnum; i++){
	// 	printf("%d ", temp->pfn);
	// 	temp = temp->nextNode;
	// }
	// printf("\n");
	// SwapNode *temp2 = swapList;
	// for(int i=0; i<sfnum;i++){
	// 	printf("%d ", temp2->index);
	// 	temp2 = temp2->nextNode;
	// }
	// printf("'\n");
}

unsigned short getFreeSpaceIndex(int isPD){     //인자로 page directory를 위한 공간을 찾는 것인지에 대한 정보를 받는다. 맞다면 freelist의 swapable을 0으로 설정 
	FreeNode *currentPcb = freeList;

	for(int i = 0; i < pfnum; i++){
		if(!currentPcb -> used){
			currentPcb -> used = 1;
			if(isPD) currentPcb -> swapable = 0;
			return currentPcb -> pfn;
		}
		currentPcb = currentPcb -> nextNode;
	}
	return 4096;							//free space가 없다면 안쓰는 pfn인 4096을 반환 (pfn의 범위는 0 ~ 4095 이다.)
}

void returnFreeSpaceIndex(unsigned short pfn){
	FreeNode *currentPcb = freeList;

	for(int i = 0; i < pfnum; i++){
		if(currentPcb -> pfn == pfn){
			currentPcb -> used = 0;
			currentPcb -> swapable = 1;
			break;
		}
		currentPcb = currentPcb -> nextNode;
	}
}

void swapIn(unsigned short swap_index){
	SwapNode *currentNode = swapList;

	for(int i = 1; i < sfnum; i++){
		if(currentNode -> index == swap_index){
			currentNode -> used = 0;
			currentNode -> pid = 11;
			break;
		}
		currentNode = currentNode -> nextNode;
	}
}

unsigned short swapOut(unsigned short pid){
	SwapNode *currentNode = swapList;

	for(int i = 1; i < sfnum; i++){
		if(!currentNode -> used){
			currentNode -> used = 1;
			currentNode -> pid = pid;
			return currentNode -> index;
		}
		currentNode = currentNode -> nextNode;
	}
	return 16384;
}

void dequeuePgtbFromEvic(unsigned short pt_index){
	EvictNode *currentNode = startEvict;
	EvictNode *postNode;

	while(currentNode -> nextNode != NULL){
		if(currentNode -> pfn == pt_index){
			postNode -> nextNode = currentNode -> nextNode;
			free(currentNode);
			break;
		}
		postNode = currentNode;
		currentNode = currentNode -> nextNode;
	}
}

unsigned short eviction(){
	if(startEvict == NULL){
		return 4096;
	}

	unsigned short result = startEvict -> pfn;
	unsigned short swapIndex = swapOut(startEvict -> pid);

	unsigned short *ptbr = (unsigned short *)(pmem + (result << 6));
	unsigned short *swbr = (unsigned short *)(swaps + (swapIndex << 6));

	//dirty bit 판단해서 0이면 그냥 out 시켜버리기 
	if((*ptbr) & 0x2){

		for(int i = 0; i < 32; i++){
			*(unsigned short*)(ptbr + i) =0 ;
		}

		EvictNode *evictedPage = startEvict;
		unsigned short return_alue = evictedPage -> pfn;
		free(evictedPage);
		startEvict = startEvict -> nextNode;
		
		return return_alue;
	}

	for(int i = 0; i < 32; i++){
		*(unsigned short*)(swbr + i) = *(unsigned short*)(ptbr + i);
		*(unsigned short*)(ptbr + i) = 0;
	}

	*(startEvict -> pte) = (swapIndex << 2) | 0x0000; 

	EvictNode *evictedPage = startEvict;
	unsigned short return_alue = evictedPage -> pfn;
	free(evictedPage);
	startEvict = startEvict -> nextNode;
	
	return return_alue;
}

int ku_proc_init(int argc, char *argv[]){

	FILE *input = fopen("input.txt","r");

	int pid; 
	char fileName[1024];
	char d;
	int segment_start;
	int segment_size;

	struct pcb *currentPcb = startPcb;
	while (!feof(input)) {
        fscanf(input, "%d %s", &pid, fileName);
		
		//새로운 pcb 생성
		if(startPcb == NULL){
			startPcb = (struct pcb *)malloc(sizeof(struct pcb));
			startPcb -> nextPcb = NULL;
			currentPcb = startPcb;
		}
		else{
			currentPcb -> nextPcb = (struct pcb *)malloc(sizeof(struct pcb));
			currentPcb = currentPcb -> nextPcb;
			currentPcb -> nextPcb = NULL;
		}

		//pcb의 pid, fd 초기화
		currentPcb -> pid = pid;
		currentPcb -> fd = fopen(fileName, "r");
		
		//page directory 생성
		unsigned short baseIndex = getFreeSpaceIndex(1);
		if(baseIndex == 4096){
			//eviction
			unsigned short result = eviction();
			if(result == 4096){
				return 1;
			}
			else{
				return result;
			}
		}
		currentPcb -> pgdir = (unsigned short *)(pmem + (baseIndex << 6));

		//page directory를 처음 생성하는 것이므로 0으로 초기화
		for(char *i = currentPcb -> pgdir; i < currentPcb -> pgdir + 32; i++){
			*i = 0;
		}

		//										<<page directory 주소가 잘 할당되었는지 테스트>>
		// printf("pmem: %d\n", pmem);
		// printf("pdbr: %d\n", pmem + (baseIndex << 6));
	
		//exe 파일을 읽어서 segment 정보 pcb에 저장
		fscanf(currentPcb -> fd, "%c\n%d %d", &d, &segment_start, &segment_size);
		currentPcb -> segment_start = segment_start;
		currentPcb -> segment_size = segment_size;
		//printf("%d, %d\n", currentPcb -> segment_start,currentPcb -> segment_size);

		currentPcb -> pd_index = baseIndex;

		//										<<fd가 instructer의 첫부분을 가르키고 있는지 테스트>>
		// int temp = fgetc(currentPcb -> fd);
		// printf("%c\n", (unsigned char)temp);

		//										<<pcb가 잘 생성되었는지 테스트>>
		// printf("pfn : %d\n", baseIndex);
		// printf("pid : %d\n", currentPcb -> pid);
		// printf("fd : %d\n", currentPcb -> fd);
		// printf("pgdir : %d\n", currentPcb -> pgdir);
		// printf("segment_start : %d\n", currentPcb -> segment_start);
		// printf("segment_size : %d\n", currentPcb -> segment_size);
    }

}

int ku_scheduler(unsigned short arg1){

	if(startPcb == NULL){
		return 1;
	}
	
	struct pcb *currentPcb = startPcb;

	if(arg1 == 10){
		current = startPcb;
		pdbr = startPcb -> pgdir;
		return 0;
	}

	//현재 실행 중인 process만이 존재하는 상황 (== 실행가능한 pcb가 없는 상태) 에서는 return 1
	if(startPcb -> pid == arg1 && startPcb -> nextPcb == NULL){
		return 1;
	}

	// 실행가능한 pcb를 Round Robin으로 찾는다. 
	//PCB Linked_list에서 인자로 들어온 pid 와 일치하는 노드 찾기 
	while(1){
		if(currentPcb -> pid == arg1+1){
			current = currentPcb;
			pdbr = currentPcb -> pgdir;
			return 0;
		}
		if(currentPcb -> nextPcb == NULL){
			current = startPcb;
			pdbr = startPcb -> pgdir;
			return 0;
		}
		currentPcb = currentPcb -> nextPcb;
	}
}



int ku_pgfault_handler(unsigned short arg1){

	//ku_dump_pmem(260);
	
	if(arg1 < current -> segment_start || arg1 >= current -> segment_start + current -> segment_size ){
		return 1;
	}

	int pd_index, pt_index, swap_index;
	unsigned short *ptbr, *realAddress, *swbr;
	unsigned short *pte, *pde;
	int PFN;

	pd_index = (arg1 & 0xFFC0) >> 11;
	pde = pdbr + pd_index;

	//만약 pde의 present bit가 0이라면
	if(!(*pde & 0x1)){

		if(*pde == 0){  //page table이 mapping 되지 않은 상태 --> 처음 접근하는 상태
			
			//page table 를 위한 freelist 받아오기
			unsigned short pfnIndex = getFreeSpaceIndex(0);
			if(pfnIndex == 4096){
				unsigned short result = eviction();
				if(result == 4096){
					return 1;
				}
				else{
					pfnIndex = result;
				}
			}

			//주소값이 참조하는 값을 바꿔버림 --> pde update
			*pde = (pfnIndex << 4) | 0x0001;
			
			//evictList에 추가해줘야 함. 
			if(startEvict == NULL && endEvict == NULL){
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex;
				endEvict -> pte = pde;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				startEvict = endEvict;
			}else{
				EvictNode *temp = endEvict;
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex;
				endEvict -> pte = pde;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				temp -> nextNode = endEvict;
			}
			
			ptbr = (unsigned short *)(pmem + (pfnIndex << 6));

			for(char *i = ptbr; i < ptbr + 32; i++){
				*i = 0;
			}

			pt_index = (arg1 & 0x07C0) >> 6;
			pte = ptbr + pt_index;
			
			//위에서 찾은 free space는 page table을 위한 것, 여기서 찾는 free space는 virtual address space를 할당하기 위한 것
			unsigned short pfnIndex2 = getFreeSpaceIndex(0);
			if(pfnIndex2 == 4096){
				unsigned short result = eviction();
				if(result == 4096){
					return 1;
				}
				else{
					pfnIndex2 = result;
				}
			}
			
			//근데 만약 수정하고자 하는 pte 가 속하는 page table이 swap out 되어 있는 상태라면...???
			if(!(*pde & 0x1) && *pde != 0){ //page table이 swap out
				unsigned short result = eviction();
				if(result == 4096){
					//page table을 메모리로 되돌리고, 넣으려는 page를 swap 공간으로 이동시킨다.
				}
				else{
					//넣으려는 page를 삽입하고 page table을 메모리로 다시 되돌리고, pte 수정
					
				}
			}
			
			//주소값이 참조하는 값을 바꿔버림 --> pte update
			*pte = (pfnIndex2 << 4) | 0x0001;

			realAddress = (unsigned short *)(pmem + (pfnIndex2 << 6));

			for(char *i = realAddress; i < realAddress + 32; i++){
				*i = 0;
			}

			//evictList에 추가해줘야 함.  
			if(startEvict == NULL && endEvict == NULL){
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex2;
				endEvict -> pte = pte;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				startEvict = endEvict;
			}else{
				EvictNode *temp = endEvict;
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex2;
				endEvict -> pte = pte;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				temp -> nextNode = endEvict;
			}
			
			//free list update는 freelist 할당 받는 함수에서 해준다. 
			// printf("이건 처음 접근하는 경우\n");
			// printf("%d의 pt는 %d, 주소는 %d\n", current -> pid, pfnIndex, ptbr);
			// printf("%d의 realAddress는 %d, 주소는 %d\n", current -> pid, pfnIndex2, realAddress);

			return 0;
		}
		else{  			//page table이 swap out 된 상태 

			//page table 를 위한 freelist 받아오기
			unsigned short pfnIndex = getFreeSpaceIndex(0);
			if(pfnIndex == 4096){
				unsigned short result = eviction();
				if(result == 4096){
					return 1;
				}
				else{
					pfnIndex= result;
				}
			}

			swap_index = (*pde & 0xFFFC) >> 2;
			swbr = (unsigned short*)(swaps + 64 + (swap_index << 6));

			ptbr = (unsigned short *)(pmem + (pfnIndex << 6));

			for(int i = 0; i < 32; i++){
				*(unsigned short*)(ptbr + i) = *(unsigned short*)(swbr + i);
				*(unsigned short*)(swbr + i) = 0;
			}

			*pde = (pfnIndex << 4) | 0x0001;

			//swap list 관리 해줘야 함. 
			swapIn(swap_index);

			//evictList에 추가해줘야 함. 
			//page table이 swap out 되어 있었다는 것은 pte가 유효한 것이 없었다는 뜻이므로 eviction list에 넣어주어야 한다.
			if(startEvict == NULL && endEvict == NULL){
				startEvict = (EvictNode *)malloc(sizeof(EvictNode));
				startEvict -> pfn = pfnIndex;
				startEvict -> pte = pde;
				startEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				endEvict = startEvict;
			}else{
				EvictNode *temp = endEvict;
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex;
				endEvict -> pte = pde;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				temp -> nextNode = endEvict;
			}

			// printf("이건 swap out 난 page table을 다시 가져오는 경우\n");
			// printf("%d의 pt는 %d, 주소는 ??\n", current -> pid, pfnIndex);

		}
    }

	//만약 pde의 present bit는 1인데, pte의 present bit가 0 인 경우 --> swap out인 경우 또는 swap out이 아닐 수도 있다. 
	else{

		PFN = (*pde & 0xFFF0) >> 4;
		ptbr = (unsigned short*)(pmem + (PFN << 6));

		pt_index = (arg1 & 0x07C0) >> 6;                      
		pte = ptbr + pt_index;

		if(!(*pte & 0x1) && *pte != 0){  //swap out인 경우

			//page를 위한 freelist 받아오기
			unsigned short pfnIndex = getFreeSpaceIndex(0);
			if(pfnIndex == 4096){
				unsigned short result = eviction();
				if(result == 4096){
					return 1;
				}
				else{
					pfnIndex= result;
				}
			}

			swap_index = (*pte & 0xFFFC) >> 2;
			swbr = (unsigned short*)(swaps + 64 + (swap_index << 6));

			for(int i = 0; i < 32; i++){
				*(unsigned short*)(ptbr + i) = *(unsigned short*)(swbr + i);
				*(unsigned short*)(swbr + i) = 0;
			}

			*pte = (pfnIndex << 4) | 0x0001;

			//pte가 유효한 값이 되었으니 page table을 evictList에서 제거
			dequeuePgtbFromEvic(pt_index);

			//swap list 관리 해줘야 함. 
			swapIn(swap_index);

			//evictList에 추가해줘야 함. 
			if(startEvict == NULL && endEvict == NULL){
				startEvict = (EvictNode *)malloc(sizeof(EvictNode));
				startEvict -> pfn = pfnIndex;
				startEvict -> pte = pde;
				startEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				endEvict = startEvict;
			}else{
				EvictNode *temp = endEvict;
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex;
				endEvict -> pte = pde;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				temp -> nextNode = endEvict;
			}
	
			// printf("이건 swap out 난 page를 다시 가져오는 경우\n");
			// printf("%d의 pt는 %d, 주소는 %d\n", current -> pid, pfnIndex, ptbr);
		}
		else{						//swap out이 아닌 경우
			//위에서 찾은 free space는 page table을 위한 것, 여기서 찾는 free space는 virtual address space를 할당하기 위한 것
			unsigned short pfnIndex2 = getFreeSpaceIndex(0);
			if(pfnIndex2 == 4096){
				unsigned short result = eviction();
				if(result == 4096){
					return 1;
				}
				else{
					pfnIndex2= result;
				}
			}
			
			//주소값이 참조하는 값을 바꿔버림 --> pte update
			*pte = (pfnIndex2 << 4) | 0x0001;

			//pte가 유효한 값이 되었으니 page table을 evictList에서 제거
			dequeuePgtbFromEvic(pt_index);

			realAddress = (unsigned short *)(pmem + (pfnIndex2 << 6));

			for(char *i = realAddress; i < realAddress + 32; i++){
				*i = 0;
			}

			//evictList에 추가해줘야 함. 
			if(startEvict == NULL && endEvict == NULL){
				startEvict = (EvictNode *)malloc(sizeof(EvictNode));
				startEvict -> pfn = pfnIndex2;
				startEvict -> pte = pte;
				startEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				endEvict = startEvict;
			}else{
				EvictNode *temp = endEvict;
				endEvict = (EvictNode *)malloc(sizeof(EvictNode));
				endEvict -> pfn = pfnIndex2;
				endEvict -> pte = pte;
				endEvict -> nextNode = NULL;
				endEvict -> pid = current -> pid;
				temp -> nextNode = endEvict;
			}
			// printf("이건 page table은 있지만 pte는 mapping이 되지 않은 경우\n");
			// printf("%d의 realAddress는 %d, 주소는 %d\n", current -> pid, pfnIndex2, realAddress);
			return 0;
		}
	}
}


int ku_proc_exit(unsigned short arg1){

	struct pcb *currentPcb = startPcb;

	//삭제할 pcb를 currentPcb 변수가 참조하게 함.
	if(currentPcb -> pid == arg1){
		startPcb = currentPcb -> nextPcb;
	}
	else{
		while(1){
			struct pcb *postPcb = currentPcb;
			currentPcb = currentPcb -> nextPcb;

			if(currentPcb -> pid == arg1){
				postPcb -> nextPcb = currentPcb -> nextPcb;
				break;
			}
			if(currentPcb -> nextPcb == NULL){
				return 1;
			}
		}
	}

	// 물리 메모리 공간 삭제, freelist 업데이트 진행 
	unsigned short *pde, *ptbr, *pte;
	int PFN, PFN2;
	for(int i = 0; i < 32; i++){

		pde = currentPcb -> pgdir + i;

		if(*pde & 0x1){
			PFN = (*pde & 0xFFF0) >> 4;
			ptbr = (unsigned short*)(pmem + (PFN << 6));

			for(int j = 0; j < 32; j++){
				
				pte = ptbr + j;

				if(*pte & 0x1){
					PFN2 = (*pte & 0xFFF0) >> 4;
					//printf("지운다 realAddress Space %d\n", PFN2);
					returnFreeSpaceIndex(PFN2); 
				}
				*(unsigned short*)(ptbr + j) = 0;
			}
			//printf("지운다 pt %d\n", PFN);
			returnFreeSpaceIndex(PFN);
		}

		*pde = 0;
	}
	//printf("지운다 pd %d\n", currentPcb -> pd_index);
	returnFreeSpaceIndex(currentPcb -> pd_index);

	//swap 공간 삭제, freelist 업데이트 진행 
	SwapNode *currentNodeSwap = swapList;
	for(int i = 1; i < sfnum; i++){
		if(currentNodeSwap -> pid == arg1){
			currentNodeSwap -> used = 0;
			currentNodeSwap -> pid = 11;
		}
	}

	free(currentPcb);

	return 0;
}