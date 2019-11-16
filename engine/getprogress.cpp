#include <iostream>
#include <fstream>
#include <unistd.h>


using namespace std;

string getProgress()
{
	ifstream myfin("/tmp/transcodeprogress");
	string progress;

	if (!myfin){
		cout << "Cannot open the file\n" << endl;
	} else {
		myfin >> progress;
	}
	myfin.close();
	return progress;
}


int main(int argc, char* argv[]){
	
	while(true){
		string progress = getProgress();
		static string lastprogress;

		cout << "progress: " << progress << endl;
		if ((!progress.empty()) && (0 == progress.compare(lastprogress))){
			//break;
		}
		sleep(1);
		lastprogress = progress;
	
	
	}





}
