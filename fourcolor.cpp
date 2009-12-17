#include <boost/scoped_array.hpp>
#include <iostream>
#include <list>
#include <vector>
#include <cmath>
#include <map>
#include <time.h>

#define LEN 17

// 2 or 4 supported. 
#define COLORS 4

int maxFound = 0;
time_t start;

using namespace std;
using boost::scoped_array;

struct Coloring
{
    int r;
    int c;
    int color;
};


// Honestly, C++ committee, is leaving this out REALLY better?
inline unsigned long long pow(int m, int e)
{
    unsigned long long ret=1;
    unsigned long long add=m;

    while(e){
	if(e&1 != 0){
	    ret*=add;
	}
	add*=add;
	e>>=1;
    }
    return ret;
}


// Returns the size of a mask based on the number of colors in this scenario
unsigned int MaskSizeFromColors(int colors)
{
    switch(colors){
    case 0:
    case 1:
    case 2:
	return 1;
    case 3:
    case 4:
	return 2;
    case 5:
    case 6:
    case 7:
    case 8:
	return 3;
    }
    return -1;
}

// An integer code is used to iterate through the search space.
// 2 colors: 010b = color 1, color 2, color 1
// 4 colors: 00011011 = color0, color1, color 2, color 3 (goes in pairs)
// If we have a square to 4-color:
//
//   1x
//   xx
//
// The number 000110 would fill in:
//
// 13
// 12
//
// I.E. it wraps around counterclockwise
int ColorFromCode(unsigned long long num, int index, int colors)
{
    int maskSize = MaskSizeFromColors(colors);

    return ((num >> (index * maskSize)) & (pow(2, maskSize)-1)) + 1 ;
}


// Converts an integer code into a vector of Colorings
void NumCodeToColorings(unsigned long long toAdd, int colors, int level,
			vector<Coloring> &colorings)
{
    int slots = 2*level-1;

    colorings.resize(slots);
    unsigned maskSizeBits = MaskSizeFromColors(colors);
    unsigned mask = (1<<maskSizeBits)-1; // msb=2 => (1<<2)-1 => 0x3

    int ind = 0;
    
    // Generate column added on left
    for(int i=0; i<level; ++i){
	colorings[ind].r = i;
	colorings[ind].c = level-1;
	colorings[ind++].color = ColorFromCode(toAdd, i, colors);
	mask<<=2;
    }
    
    // Generate row added on bottom
    for(int i=0; i<level-1; ++i){
	colorings[ind].r = level-1;
	colorings[ind].c = i;
	colorings[ind++].color
	    = ColorFromCode(toAdd, slots-1-i, colors);
	mask<<=2;
    }
}


// Sets all positions on a board to 0
void ClearBoard(int board[LEN][LEN])
{
    for(int r=0; r<LEN; ++r)
	for(int c=0; c<LEN; ++c)
	    board[r][c] = 0;
}

// Prints all positions on the board to STDOUT
void PrintBoard(int board[LEN][LEN])
{
    for(int r=0; r<LEN; ++r){
	for(int c=0; c<LEN; ++c){
	    cout<<board[r][c];
	}
	cout<<endl;
    }
}


// Draws an integer code onto the board by level and number of colors.
void DrawOnBoard(int board[LEN][LEN], int level, int colors,
		 unsigned long long toAdd)
{
    static vector<Coloring> coloring;
    NumCodeToColorings(toAdd, colors, level, coloring);
    for(int i=0; i<coloring.size(); ++i){
	board[coloring[i].r][coloring[i].c] = coloring[i].color;
    }
}

// When iterating through integer codes at a particular level, we will
// inevitably hit constraints based on numbers that had been placed in
// previous levels. Instead of iterating by 1, we can skip all of the
// numbers that match the lowest bit of the conflict.
unsigned long long CalculateJump(int slot, unsigned long long num, int colors)
{
    unsigned long long maskSize = MaskSizeFromColors(colors),
	one, slotCalc;
    slotCalc = slot-1;
    one=1;
    unsigned long long mod = one<<(slotCalc * maskSize);
    return mod - (num % mod);
}

/*
// Counts the constraints that the most recent addition at 'level'
// added.
int CountAddedConstraints(int board[LEN][LEN], int level)
{
    int acc=0;
    for(int r=0; r<level; ++r){
	int rowCol = board[r][level-1];
	for(int c=0; c<level; ++c){
	    int colCol = board[level-1][c];
	    if(r < level-1){
		if(board[r][c] == rowCol)
		    ++acc;
	    }
	    if(c < level-1){
		if(board[r][c] == colCol)
		    ++acc;
	    }
	}
    }
    return acc;
}
*/

// Accumulates a distribution of the number of constraints added by
// the most recent number addition at 'level'. Returns the standard
// deviation of this distribution.
double GenerateConstraintScore(int board[LEN][LEN], int level)
{
    int constraints[LEN];
    for(int i=0; i<LEN; ++i) constraints[i] = 0;
    
    for(int r=0; r<level; ++r){
	int rowCol = board[r][level-1];
	for(int c=0; c<level; ++c){
	    int colCol = board[level-1][c];
	    if(r < level-1){
		if(board[r][c] == rowCol)
		    ++constraints[rowCol-1];
	    }
	    if(c < level-1){
		if(board[r][c] == colCol)
		    ++constraints[colCol-1];
	    }
	}
    }
    int acc=0;
    for(int i=0; i<LEN; ++i) acc+=constraints[i];
    double mean = acc / (1.0 * LEN);

    double stdDevAcc = 0.;

    for(int i=0; i<LEN; ++i){
	stdDevAcc+=(pow(mean - constraints[i], 2));
    }
    return sqrt(stdDevAcc);
}

// Iterates through the search space, and finds the list of
// candidates.  I did some benchmarking, and aborting on the first
// mismatch is much faster than finding the best mismatch.
void FindCandidates(int board[LEN][LEN],
		    multimap<double, unsigned long long> &candidateMap,
		    int colors,
		    int level)
{
    unsigned long long max = pow(colors, 2*level-1);
    unsigned long long rowEndColor, colEndColor;

    unsigned long long candidate = 0;
    while(candidate < max){
	DrawOnBoard(board, level, colors, candidate);
	int bestInvalidSlot = 0;

	// Search for 2 columns that are invalidated. If we hit, we
	// are invalidated on a very high bit, so this gives us the
	// biggest jump in our search.
	for(int c0=0; c0<level-2; ++c0){
	    int c0Col=board[level-1][c0];

	    for(int c1=c0+1; c1<level-1; ++c1){
		int c1Col=board[level-1][c1];

		if(c0Col == c1Col){
		    for(int r=0; r<level-1; ++r){
			if(board[r][c0] == board[r][c1]
			   && board[r][c0] == c0Col){
			    //			    cerr<<"invalid col: "<<c0<<" "<<c1<<" "<<(level + (level-c1-1))<<endl;
			    int nextSlot = (level + (level-c1-1));

			    bestInvalidSlot =
				bestInvalidSlot > nextSlot
				? bestInvalidSlot
				: nextSlot ;
			    // If you think of a better way,
			    // jakevoytko@gmail.com :)
			    goto abort;
		        }
		    }
		}
	    }
	}
	//if(bestInvalidSlot > 0)
	//goto abort;

	// Now try to strike out on a low bit
	// This still could give us a nice jump, but not good enough
	for(int r0=0; r0<level-1; ++r0){
	    int r0Col = board[r0][level-1];
	    
	    for(int r1=r0+1; r1<level; ++r1){
		int r1Col = board[r1][level-1];

		if(r0Col == r1Col){
		    for(int c=0; c<level-1; ++c){
			if(board[r0][c] == board[r1][c]
			   && board[r0][c] == r0Col){
			    //			    cerr<<"invalid row: "<<r0<<" "<<r1<<" "<<(r0 + 1)<<endl;
			    int nextSlot = r0 + 1;
			    bestInvalidSlot = bestInvalidSlot  > nextSlot
				? bestInvalidSlot
				: nextSlot;
			    goto abort;
			}
		    }
		}
	    }
	}
    abort:
	// If we haven't found an invalid slot.
	if(0 == bestInvalidSlot ){
	    double numAdded = GenerateConstraintScore(board, level);
	    candidateMap
		.insert(pair<double, unsigned long long>(numAdded, candidate));
	    ++candidate;

	    // Good enough for me
	    if(candidateMap.size() > 20000){
		return;
	    }
	    else if(level > 9 && candidateMap.size() > 1000)
		return;
	    else if(level > 11 && candidateMap.size() > 10)
		return;
	}
	else
	    candidate+=CalculateJump(bestInvalidSlot, candidate, colors);
    }
}

bool Recur(int board[LEN][LEN], int colors, int level)
{
    typedef multimap<double, unsigned long long> multiMapIntUInt64;
    if(level > maxFound){
	maxFound = level;
	cout<<"Solved level "<<level-1<<" in ";
	time_t diff = time(NULL) - start;
	cout<<diff<<" seconds"<<endl;
	PrintBoard(board);
    }
	  
    if(level <= LEN){
	multiMapIntUInt64 candidateMap;
	cerr<<"Finding candidates for level "<<level<<endl;
	FindCandidates(board, candidateMap, colors, level);
	cerr<<"Done finding candidates for level "<<level<<endl;
	multiMapIntUInt64::iterator itr = candidateMap.begin(),
	    end=candidateMap.end();
	for(; itr!=end; ++itr){
	    DrawOnBoard(board, level, colors, itr->second);
	    if(Recur(board, colors, level+1)){
		return true;
	    }
	    //cerr<<"level "<<level<<": "<<itr->second<<" sucks"<<endl;
	}
	return false;
    }
    else{
	return true;
    }
}

bool Solve(int board[LEN][LEN], int colors)
{
    return Recur(board, colors, 1);
}

int main()
{
    int board[LEN][LEN];
    ClearBoard(board);
    
    start = time(NULL);
    
    if(!Solve(board, COLORS)){
	// Knowing we failed is probably wishful thinking
	cout<<"There are no "<<LEN<<" x "<<LEN<<" solutions for "
	    <<COLORS<<" colors"<<endl;
    }
    else{
	cout<<"Solution found!"<<endl;
	PrintBoard(board);
    }
    
    return 0;
}
