/**
 * Computer player
 *
 * (1) Connects to a game communication channel,
 * (2) Waits for a game position requiring to draw a move,
 * (3) Does a best move search, and broadcasts the resulting position,
 *     Jump to (2)
 *
 * (C) 2005-2015, Josef Weidendorfer, GPLv2+
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "board.h"
#include "search.h"
#include "eval.h"
#include "network.h"


/* Global, static vars */
NetworkLoop l;
Board myBoard;
Evaluator ev;

/* Which color to play? */
int myColor = Board::color1;

/* Which search strategy to use? */
int strategyNo = 2;

/* Max search depth */
int maxDepth = 0;

/* Maximal number of moves before terminating (negative for infinity) */
int maxMoves = -1;

/* to set verbosity of NetworkLoop implementation */
extern int verbose;

/* remote channel */
char* host = 0;       /* not used on default */
int rport = 23412;

/* local channel */
int lport = 23412;

/* change evaluation after move? */
bool changeEval = true;




/**
 * MyDomain
 *
 * Class for communication handling for player:
 * - start search for best move if a position is received
 *   in which this player is about to draw
 */
class MyDomain: public NetworkDomain
{
public:
    MyDomain(int p) : NetworkDomain(p) { sent = 0; }

    void sendBoard(Board*);

protected:
    void received(char* str);
    void newConnection(Connection*);

private:
    Board* sent;
};

void MyDomain::sendBoard(Board* b)
{
    if (b) {
	static char tmp[500];
	sprintf(tmp, "pos %s\n", b->getState());
	if (verbose) printf("%s", tmp+4);
	broadcast(tmp);
    }
    sent = b;
}

void MyDomain::received(char* str)
{
    if (strncmp(str, "quit", 4)==0) {
	l.exit();
	return;
    }

    if (strncmp(str, "pos ", 4)!=0) return;

    // on receiving remote position, do not broadcast own board any longer
    sent = 0;

    myBoard.setState(str+4);
    if (verbose) {
	printf("\n\n==========================================\n%s", str+4);
    }

    int state = myBoard.validState();
    if ((state != Board::valid1) && 
	(state != Board::valid2)) {
	printf("%s\n", Board::stateDescription(state));
	switch(state) {
	    case Board::timeout1:
	    case Board::timeout2:
	    case Board::win1:
	    case Board::win2:
		l.exit();
	    default:
		break;
	}
	return;
    }

    if (myBoard.actColor() & myColor) {
	struct timeval t1, t2;

	gettimeofday(&t1,0);
	Move m = myBoard.bestMove();
	gettimeofday(&t2,0);

	int msecsPassed =
	    (1000* t2.tv_sec + t2.tv_usec / 1000) -
	    (1000* t1.tv_sec + t1.tv_usec / 1000);

	printf("%s ", (myColor == Board::color1) ? "O":"X");
	if (m.type == Move::none) {
	    printf(" can not draw any move ?! Sorry.\n");
	    return;
	}
	printf("draws '%s' (after %d.%03d secs)...\n",
	       m.name(), msecsPassed/1000, msecsPassed%1000);

	myBoard.playMove(m, msecsPassed);
	sendBoard(&myBoard);

	if (changeEval)
	    ev.changeEvaluation();

	/* stop player at win position */
	int state = myBoard.validState();
	if ((state != Board::valid1) && 
	    (state != Board::valid2)) {
	    printf("%s\n", Board::stateDescription(state));
	    switch(state) {
		case Board::timeout1:
		case Board::timeout2:
		case Board::win1:
		case Board::win2:
		    l.exit();
		default:
		    break;
	    }
	}

	maxMoves--;
	if (maxMoves == 0) {
	    printf("Terminating because given number of moves drawn.\n");
	    broadcast("quit\n");
	    l.exit();
	}
    }    
}

void MyDomain::newConnection(Connection* c)
{
    NetworkDomain::newConnection(c);

    if (sent) {
	static char tmp[500];
	int len = sprintf(tmp, "pos %s\n", sent->getState());
	c->sendString(tmp, len);
    }
}

/*
 * Main program
 */

void printHelp(char* prg, bool printHeader)
{
    if (printHeader)
	printf("Computer player V 0.2\n"
	       "Search for a move on receiving a position in which we are expected to draw.\n\n");

    printf("Usage: %s [options] [X|O] [<strength>]\n\n"
	   "  X                Play side X\n"
	   "  O                Play side O (default)\n"
	   "  <strength>       Playing strength, depending on strategy\n"
	   "                   A time limit can reduce this\n\n" ,
	   prg);
    printf(" Options:\n"
	   "  -h / --help      Print this help text\n"
	   "  -v / -vv         Be verbose / more verbose\n"
	   "  -s <strategy>    Number of strategy to use for computer (see below)\n"
	   "  -n               Do not change evaluation function after own moves\n"
	   "  -<integer>       Maximal number of moves before terminating\n"
	   "  -p [host:][port] Connection to broadcast channel\n"
	   "                   (default: 23412)\n\n");

    printf(" Available search strategies for option '-s':\n");

    const char** strs = SearchStrategy::strategies();
    for(int i = 0; strs[i]; i++)
	printf("  %2d : Strategy '%s'%s\n", i, strs[i],
	       (i==strategyNo) ? " (default)":"");
    printf("\n");

    exit(1);
}

void parseArgs(int argc, char* argv[])
{
    int arg=0;
    while(arg+1<argc) {
	arg++;
	if (strcmp(argv[arg],"-h")==0 ||
	    strcmp(argv[arg],"--help")==0) printHelp(argv[0], true);
	if (strncmp(argv[arg],"-v",2)==0) {   
	    verbose = 1;
	    while(argv[arg][verbose+1] == 'v') verbose++;
	    continue;
	}
	if (strcmp(argv[arg],"-n")==0)	{
	    changeEval = false;
	    continue;
	}
	if ((strcmp(argv[arg],"-s")==0) && (arg+1<argc)) {
	    arg++;
	    if (argv[arg][0]>='0' && argv[arg][0]<='9')
               strategyNo = argv[arg][0] - '0';
            continue;
        }

	if ((argv[arg][0] == '-') &&
	    (argv[arg][1] >= '0') &&
	    (argv[arg][1] <= '9')) {
	    int pos = 2;

	    maxMoves = argv[arg][1] - '0';
	    while((argv[arg][pos] >= '0') &&
		  (argv[arg][pos] <= '9')) {
		maxMoves = maxMoves * 10 + argv[arg][pos] - '0';
		pos++;
	    }
	    continue;
	}

	if ((strcmp(argv[arg],"-p")==0) && (arg+1<argc)) {
	    arg++;
	    if (argv[arg][0]>'0' && argv[arg][0]<='9') {
		lport = atoi(argv[arg]);
		continue;
	    }
	    char* c = strrchr(argv[arg],':');
	    int p = 0;
	    if (c != 0) {
		*c = 0;
		p = atoi(c+1);
	    }
	    host = argv[arg];
	    if (p) rport = p;
	    continue;
	}
	
	if (argv[arg][0] == 'X') {
	    myColor = Board::color2;
	    continue; 
	}
	if (argv[arg][0] == 'O') {
	    myColor = Board::color1;
	    continue;
	}

	int strength = atoi(argv[arg]);
	if (strength == 0) {
	    printf("ERROR - Unknown option %s\n", argv[arg]);
	    printHelp(argv[0], false);
	}

	maxDepth = strength;
    }
}

int main(int argc, char* argv[])
{
    parseArgs(argc, argv);

    SearchStrategy* ss = SearchStrategy::create(strategyNo);
    ss->setMaxDepth(maxDepth);
    printf("Using strategy '%s' (depth %d) ...\n", ss->name(), maxDepth);

    myBoard.setSearchStrategy( ss );
    ss->setEvaluator(&ev);
    ss->registerCallbacks(new SearchCallbacks(verbose));

    MyDomain d(lport);
    l.install(&d);

    if (host) d.addConnection(host, rport);

    l.run();

}
