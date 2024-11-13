//h264qp.cpp
#include "stdio.h"
#include "windows.h"
#include "math.h"
#include <filesystem>
#include <thread>

#define LINE_BUFFER_SIZE 7000 //sufficiente anche per video 8k
#define ISNUM(x) ((x>='0')&&(x<='9'))
#define ISNUM_(x) (((x>='0')&&(x<='9'))||(x==' '))
#define QPV(x) (x==' ' ? 0 : x-'0')

//return errors
#define ERR_MB_CHECK 12345
#define ERR_FFPROBE_NOT_FOUND 100
#define ERR_FFMPEG_NOT_FOUND 101
#define ERR_FFPROBE_PIPE_BASIC 3
#define ERR_NO_H264_STREAM 4
#define ERR_NAMEDPIPE 5
#define ERR_FFPROBE_PIPE_ANALYSIS 6
#define ERR_STDOUT_END_INVALID 7
#define ERR_FRAME_MISMATCH 8
#define ERR_OUTPUT_STATS 9

struct frameA
{
	char type;
	uint32_t size;
	uint64_t byte_position;
	
	frameA* next;
};
struct frameB
{
	double avg_qp;
	double stdevQ_qp;

	frameB* next;
};

struct shared
{
	FILE* pipeA;
	HANDLE pipeB;
	frameB* chain;
	int counterB;
};

bool checktype(frameA* f, char c)//used in global analysis
{
	if (c=='0') return true; //TOT
	if (c=='1') return ((f->type=='K')||(f->type=='I')); //K+I
	if (c=='2') return ((f->type=='P')||(f->type=='B')); //P+B
	return (f->type==c);
}

int readline(FILE* stream, char* buffer, int buf_size) { //standard pipe
	//Legge i caratteri finchè non trova '\n'
	//ritorna il numero di caratteri letti (eventualmente 0)
	//ritorna -1 in caso di errore o EOF
	//ritorna -2 in caso di overflow

	int ccounter=0;
	int c;
	char* cptr=buffer;

	while(1)
	{
		if (ccounter==buf_size-1) return -2;
		c=fgetc(stream);
		if (c==EOF)
		{
			*cptr=0;
			return -1;
		}
		if ((c=='\n')||(c==0))
		{
			*cptr=0;
			return ccounter;
		}
		*cptr=c;
		cptr++;
		ccounter++;
	}
}
int readline(HANDLE stream, char* buffer, int buf_size) { //named pipe
	//Legge i caratteri finchè non trova '\n'
	//ritorna il numero di caratteri letti (eventualmente 0)
	//ritorna -1 in caso di errore o EOF
	//ritorna -2 in caso di overflow

	int ccounter=0;
	int c;
	char* cptr=buffer;

	while(1)
	{
		if (ccounter==buf_size-1) return -2;
		
		if (!ReadFile(stream,cptr,1,NULL,NULL))
			return -1;	
		if ((*cptr=='\n')||(*cptr==0))
		{
			*cptr=0;
			return ccounter;
		}
		cptr++;
		ccounter++;
	}
}

int64_t sttoint(const char* pp)
{
	int64_t segno = 1;
	int64_t n = 0;
	if (*pp == '-')
	{
		segno = -1;
		pp++;
	}
	while ((*pp - '0' >= 0) && (*pp - '0' < 10))
	{
		n *= 10;
		n += *pp - '0';
		pp++;
	}
	return segno * n;
}

void threadF(shared* sD)
{
	char* line=new char [LINE_BUFFER_SIZE];
	char* cptr;
	int mbcount,x;
	int newframeflag=0;
	int startflag=0;//discard reduntant initial frames
	int mbcount_check=-1;
	
	double tmpd;
	
	frameB* currentFrameB=NULL;	
	sD->chain=currentFrameB;	
	ConnectNamedPipe(sD->pipeB,NULL);
	
	while(1)//FRAME CYCLE
	{		
		if (newframeflag==0) if (readline(sD->pipeB,line,LINE_BUFFER_SIZE)<0) break; //END
		
		newframeflag=0;
		
		if(strstr(line,"Processing read interval")!=NULL) startflag=1; //discard reduntant initial frames, start after this line
		
		if(startflag==1) if (line[0]=='[') if (strstr(line,"[h264 @ ")==line) if (strstr(line,"New frame, type:")!=NULL)
		{			
			if (currentFrameB==NULL) //first round
			{
				currentFrameB =new frameB;						
				sD->chain=currentFrameB;
			}
			else
			{
				currentFrameB->next =new frameB;
				currentFrameB=currentFrameB->next;
				mbcount_check=mbcount;
			}
			currentFrameB->avg_qp=0.0;
			currentFrameB->stdevQ_qp=0.0;
			currentFrameB->next=NULL;
			sD->counterB++;
			
			readline(sD->pipeB,line,LINE_BUFFER_SIZE);//useless coordinate row

			mbcount=0;
			while(1)//MACROBLOCK CYCLE
			{
				readline(sD->pipeB,line,LINE_BUFFER_SIZE);
				if (strstr(line,"[h264 @ ")==line) if (strstr(line,"New frame, type:")!=NULL)
				{
					newframeflag=1;
					break; //stop current frame parsing, do not read another line since we alreay have a new frame
				}
				if ( (strstr(line," nal_unit_type:")!=NULL )||(strstr(line,"[h264 @ ")!=line)) break; //stop current f. parsing
				cptr=line;
				while(*cptr!=']') cptr++;
				cptr++;
				while(*cptr==' ') cptr++;
				if (!ISNUM(*cptr)) break; //stop current f. parsing
				while (ISNUM(*cptr)) cptr++;
				if (*cptr!=' ') break; //stop current f. parsing
				cptr++;

				while (ISNUM_(*cptr))
				{
					tmpd=10*(QPV(*cptr));
					cptr++;
					tmpd+=QPV(*cptr);
					cptr++;
					currentFrameB->avg_qp+=tmpd;
					currentFrameB->stdevQ_qp+=tmpd*tmpd;
					
					mbcount++;
				}
			}
			
			if (mbcount_check==-1) fprintf(stderr,"[LOG] Macroblocks per frame: %d\n",mbcount);
			
			if (mbcount_check!=-1) if (mbcount_check!=mbcount)
			{
				sD->counterB=-1;
				delete [] line;
				fprintf(stderr,"[LOG] Something went wrong while parsing (mbcount_check). Closing app.");
				pclose(sD->pipeA);
				CloseHandle(sD->pipeB);
				exit(ERR_MB_CHECK);				
			}
			currentFrameB->avg_qp/=mbcount;
			currentFrameB->stdevQ_qp/=mbcount;
			currentFrameB->stdevQ_qp-=currentFrameB->avg_qp*currentFrameB->avg_qp;
		}
	}
	delete [] line;
	return;
}

int main(int argv,char** argc) {
	//////////////////////////////////////////////////////////////////////////// M A I N //////
	int build=241113;
	
	char* line=new char [LINE_BUFFER_SIZE];
	frameA* chain = NULL;
	frameA* currentFrameA;
	frameB* currentFrameB;
	char* cptr;//cursore
	shared sharedData;
	std::string cmd;

	int framecount=0;
	int x;
	
	//HELP + CHECK FILE IN argv[1]
	int help=0;
	if ((argv!=2)&&(argv!=3)) help=1;
	else if (!(std::filesystem::exists(argc[1])))
	{
		help=1;
		fprintf(stderr,"Input file:\n%s\ndoes not exist\n\n",argc[1]);
	}	
	if (help==1)
	{
		printf("h264qp - Build %d - by HG\n\n",build);
		printf("USAGE:\n");
		printf("1) h264qp.exe inputFile\n");
		printf("2) h264qp.exe inputFile outputStatsFile\n\n");
		
		printf("This script displays on stdout general QP statistics for h264 video in inputFile\n");
		printf("Optionally, it writes detailed statistics for each frame in outputStatsFile\n");
		printf("Log messages are on stderr\n");
		printf("Requires ffmpeg.exe and ffprobe.exe in same dir or PATH\n\n");
		return 0;
	}
	
	//INIT
	fprintf(stderr,"[LOG] h264qp - Build %d - by HG\n[LOG] INPUT FILE: %s\n[LOG] OUTPUT STATS: ",build,argc[1]);
	if (argv==3)
		fprintf(stderr,argc[2]);
	else
		fprintf(stderr,"# DISABLED");
	//check ffmpeg
	fprintf(stderr,"\n[LOG] Check if ffmpeg.exe is available... ");
	if (system("ffmpeg.exe -hide_banner -version >NUL")!=0)
	{
		fprintf(stderr,"\n[LOG] ffmpeg.exe not found - Exit program\n");
		return ERR_FFMPEG_NOT_FOUND;
	}
	fprintf(stderr,"OK\n");
	//check ffprobe
	fprintf(stderr,"[LOG] Check if ffprobe.exe is available... ");
	if (system("ffprobe.exe -version >NUL")!=0)
	{
		fprintf(stderr,"\n[LOG] ffprobe.exe not found - Exit program\n");
		return ERR_FFPROBE_NOT_FOUND;
	}
	fprintf(stderr,"OK\n");

	//check if h264 and if 10 bit
	int flag10=0;
	int flagh264=0;
	fprintf(stderr,"[LOG] STARTING ffprobe.exe FOR BASIC INFO\n");
	cmd=std::string("ffprobe.exe -threads 1 -v quiet -select_streams V:0 -show_streams \"")+std::string(argc[1])+std::string("\"");
	sharedData.pipeA=popen(cmd.c_str(),"rb");
	if(sharedData.pipeA==NULL)
	{
		fprintf(stderr,"[LOG] Fail opening pipe for ffprobe stdout - Exit Program");
		CloseHandle(sharedData.pipeB);
		return ERR_FFPROBE_PIPE_BASIC;
	}
	while(readline(sharedData.pipeA,line,LINE_BUFFER_SIZE)>=0)
	{
		if (strstr(line,"bits_per_raw_sample=10")==line) flag10=1;			
		if (strstr(line,"codec_name=h264")==line) flagh264=1;
		if (flag10*flagh264==1) break;//nothing more to read
	}
	pclose(sharedData.pipeA);
	if (flagh264==0)
	{
		fprintf(stderr,"[LOG] h264 stream not found - Exit program\n");
		return ERR_NO_H264_STREAM;
	}
	fprintf(stderr,"[LOG] h264 stream found --- Bit depth = %d bit",8+2*flag10);
	if (flag10==1)
		fprintf(stderr," (qp value are shifted by -12)");
	fputc('\n',stderr);
	
	//create named pipe for stderr	
	int pid=getpid();//allows for multiple runs
	sharedData.pipeB=CreateNamedPipeA(
  			std::string("\\\\.\\pipe\\ffprobe_stderr_PID"+std::to_string(pid)).c_str(),
  			PIPE_ACCESS_INBOUND,
  			PIPE_TYPE_BYTE,
  			1,
  			2048,//IS 2048 OK?
  			2048,
  			0,
  			NULL);
  	sharedData.counterB=0;
	if(sharedData.pipeB==INVALID_HANDLE_VALUE)
	{
		fprintf(stderr,"[LOG] Fail opening NamedPipe for ffprobe stderr - Exit Program");
		return ERR_NAMEDPIPE;
	}
	
	//standard pipe for stdout, start ffprobe analysis
	fprintf(stderr,"[LOG] STARTING ffmpeg.exe | ffprobe.exe FOR ANALYSIS\n");
	cmd=std::string("ffmpeg.exe -hide_banner -loglevel error -threads 1 -i \"")
			+std::string(argc[1])
			+std::string("\" -map 0:V:0 -c:v copy -f h264 pipe: | ffprobe.exe -threads 1 -v quiet -show_frames -show_streams -show_entries frame=key_frame,pkt_pos,pkt_size,pict_type -debug qp -i pipe: 2>>\\\\.\\pipe\\ffprobe_stderr_PID")
			+std::to_string(pid);
	sharedData.pipeA=popen(cmd.c_str(),"rb");
	if(sharedData.pipeA==NULL)
	{
		fprintf(stderr,"[LOG] Fail opening pipe for ffprobe stdout - Exit Program");
		CloseHandle(sharedData.pipeB);
		return ERR_FFPROBE_PIPE_ANALYSIS;
	}
	
	//THREAD for stderr
	std::thread* tB;
	tB=new std::thread(threadF,&sharedData);
	
	//PARSING STDOUT
	x=0;
	while(readline(sharedData.pipeA,line,LINE_BUFFER_SIZE)>=0)
	{
		cptr=line;
		if (strstr(line,"[FRAME]")==line)
		{
			framecount++;
			if (chain==NULL)//first round
			{
				chain=new frameA;
				currentFrameA=chain;
			}
			else
			{
				currentFrameA->next=new frameA;
				currentFrameA=currentFrameA->next;
			}
			currentFrameA->type='?';
			currentFrameA->next=NULL;

			if (framecount%1000==0)
				fprintf(stderr,"[LOG] Proccessing FRAME %d\n",framecount);
		}
		else if (strstr(line,"key_frame=")==line)
		{
			while(*cptr!='=') cptr++;
			cptr++;
			if (*cptr=='1') currentFrameA->type='K';

		}
		else if (strstr(line,"pkt_pos=")==line)
		{
			while(*cptr!='=') cptr++;
			cptr++;
			currentFrameA->byte_position=sttoint(cptr);
		}
		else if (strstr(line,"pkt_size=")==line)
		{
			while(*cptr!='=') cptr++;
			cptr++;
			currentFrameA->size=sttoint(cptr);
		}
		else if (strstr(line,"pict_type=")==line)
		{
			if(currentFrameA->type!='K')
			{
				while(*cptr!='=') cptr++;
				cptr++;
				currentFrameA->type=*cptr;
			}
		}
		else if (strstr(line,"[STREAM]")==line)
		{
			x=1;//END OK
			break;
		}
	}
	if (x!=1)
	{
		fprintf(stderr,"[LOG] Something went wrong while parsing. Closing app.");
		pclose(sharedData.pipeA);
		CloseHandle(sharedData.pipeB);
		return ERR_STDOUT_END_INVALID;
	}
	
	//COMPARE STDOUT AND STDERR + CLOSE PIPE STUFF
	fprintf(stderr,"[LOG] Waiting for stderr thread to end and check frames... ");
	tB->join();
	pclose(sharedData.pipeA);
	CloseHandle(sharedData.pipeB);
	delete tB;
	if (sharedData.counterB!=framecount)
	{
		fprintf(stderr,"FAIL (frames mismatch stdout vs stderr) %d VS %d\n",framecount, sharedData.counterB);
		return ERR_FRAME_MISMATCH;
	}	
	fprintf(stderr,"OK %d TOTAL FRAMES\n",framecount);
	
	//WRITE GLOBAL STATISTICS
	char type[] ="0BP1KI2";
	int l=strlen(type);
	uint64_t totalSize;
	uint64_t totaltotalSize;
	uint32_t n;
	double mSize;
	double mQP;
	double stdevF;
	double stdevMB;
	
	fprintf(stderr,"[LOG] WRITING STATISTIC TO STDOUT\n");
	for(x=0;x<l;x++)
	{
		double tmpd;
		currentFrameA=chain;
		currentFrameB=sharedData.chain;
		n=0;
		totalSize=0;
		totaltotalSize=0;
		mSize=0.0;
		mQP=0.0;
		stdevF=0.0;
		stdevMB=0.0;
		
		while (currentFrameA!=NULL)
		{
			totaltotalSize+=currentFrameA->size;
			if(checktype(currentFrameA,type[x]))
			{
				totalSize+=currentFrameA->size;
				n++;
				mQP+=currentFrameB->avg_qp-12.0*flag10;				
			}
			currentFrameA=currentFrameA->next;
			currentFrameB=currentFrameB->next;
		}
		mSize=totalSize;
		mSize/=n;
		mQP/=n;
		currentFrameA=chain;
		currentFrameB=sharedData.chain;
		while (currentFrameA!=NULL)
		{
			if(checktype(currentFrameA,type[x]))
			{
				tmpd=(mQP-currentFrameB->avg_qp+12.0*flag10);
				tmpd*=tmpd;
				stdevF+=tmpd;
				stdevMB+=currentFrameB->stdevQ_qp;			
				stdevMB+=tmpd;				
			}
			currentFrameA=currentFrameA->next;
			currentFrameB=currentFrameB->next;
		}
		stdevF/=n;
		stdevMB/=n;
		stdevF=sqrt(stdevF);
		stdevMB=sqrt(stdevMB);
		if (type[x]=='0') printf("TOT");
		else if (type[x]=='1') printf("K+I");
		else if (type[x]=='2') printf("P+B");
		else putchar(type[x]);
		printf(":\n");
		
		printf("N               : %d\n",n);
		printf("N/TOT           : %f %%\n",100.0*(double)(n)/(double)(framecount));
		printf("Total Size      : %s\n",std::to_string(totalSize).c_str());
		printf("Total Size/TOT  : %f %%\n",100.0*(double)(totalSize)/(double)(totaltotalSize));
		printf("Average Size    : %f\n",mSize);
		printf("AVERAGE QP      : %f\n",mQP);
		printf("StdDev (frames) : %f\n",stdevF);
		printf("StdDev (MB)     : %f\n\n",stdevMB);
	}
	//CONSECUTIVE B
	double totB=0.0;
	int maxB=0;
	int ccounterB=0;
	int* cBf=new int[16];
	for (x=0;x<16;x++) cBf[x]=0;
	currentFrameA=chain;
	while (currentFrameA!=NULL)
	{
		if (currentFrameA->type=='B')
		{
			ccounterB++;
		}
		else if(ccounterB>0)
		{
			if (ccounterB<=16) cBf[ccounterB-1]+=ccounterB;
			maxB=( ccounterB > maxB ? ccounterB : maxB );
			totB=totB+ccounterB;
			ccounterB=0;			
		}
		currentFrameA=currentFrameA->next;		
	}
	printf("Consecutive B-Frames (number of B-Frames in each group) :");
	if (maxB==0) printf("  # N/A");
	printf("\n");
	for(x=0;x<maxB;x++)
		printf("%d : %d ( %f %% )\n",x+1,cBf[x],100.0*cBf[x]/totB);
		
	delete [] cBf;
	
	//PER FRAME STATISTICS
	if (argv==3)
	{			
		FILE* out=fopen(argc[2],"wb");
		if (out==NULL)
		{
			fprintf(stderr,"[LOG] FAILED OPENING OUTPUT STATS FILE\n");
			return ERR_OUTPUT_STATS;	
		}
		currentFrameA=chain;
		currentFrameB=sharedData.chain;
		fprintf(stderr,"[LOG] WRITING PER-FRAME STATS FILE\n");
		fputs(std::to_string(framecount).c_str(),out);//frame number at first line
		fputc('\n',out);
		while (currentFrameA!=NULL)
		{
			fputc(currentFrameA->type,out);
			fputc('\t',out);
			fputs(std::to_string(currentFrameA->size).c_str(),out);
			fputc('\t',out);
			fputs(std::to_string(currentFrameA->byte_position).c_str(),out);
			fputc('\t',out);
			fputs(std::to_string(currentFrameB->avg_qp-12.0*flag10).c_str(),out);
			fputc('\t',out);
			fputs(std::to_string(sqrt(currentFrameB->stdevQ_qp)).c_str(),out);
			fputc('\n',out);
			currentFrameA=currentFrameA->next;
			currentFrameB=currentFrameB->next;			
		}
		//TODO? delete chains	
		fclose(out);
	}	
	//CLOSING
	fprintf(stderr,"[LOG] SCRIPT COMPLETE\n");	
	return 0;
}/////////////END

/*
BONUS

Create the file DragNDropHere.bat in the same dir as follows

%0\..\h264qp.exe %1 %1.dat >%1.txt

Then drag your video file on it
*/

