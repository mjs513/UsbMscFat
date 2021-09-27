/*
  MSC USB Drive datalogger
 
 This example shows how to log data from three analog sensors
 to an MSC USB drive using the mscFS library.
 
 created  24 Nov 2010
 modified 9 Apr 2012
 by Tom Igoe
 modified 17 Nov 2020
 by Warren Watson
 
 This example code is in the public domain.
 */

#include <mscFS.h>

// Setup USBHost_t36 and as many HUB ports as needed.
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
USBHub hub3(myusb);
USBHub hub4(myusb);

// Setup MSC for the number of USB Drives you are using. (Two for this example)
// Mutiple  USB drives can be used. Hot plugging is supported. There is a slight
// delay after a USB MSC device is plugged in. This is waiting for initialization
// but after it is initialized ther should be no delay.
msController msDrive1(myusb);
msController msDrive2(myusb);

int record_count = 0;
bool write_data = false;
File dataFile;

void setup()
{
  //UNCOMMENT THESE TWO LINES FOR TEENSY AUDIO BOARD:
  //SPI.setMOSI(7);  // Audio shield has MOSI on pin 7
  //SPI.setSCK(14);  // Audio shield has SCK on pin 14

  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect.
  }

  // Start USBHost_t36, HUB(s) and USB devices.
  myusb.begin();

  Serial.print("\nInitializing USB MSC drive...");

  // see if the drive is present and can be initialized:
  if (!MSC.begin(&msDrive1)) {
    Serial.println("initialization failed!");
    while (1) {
      // No USB drive, so don't do anything more - stay stuck here
    }
  }
  Serial.println("USB drive initialized.");

  menu();
}

void loop()
{
  if ( Serial.available() ) {
    char rr;
    rr = Serial.read();
    switch (rr) {
      case 'l': listFiles(); break;
      case 'e': eraseFiles(); break;
      case 's':
        {
          Serial.println("\nLogging Data!!!");
          write_data = true;   // sets flag to continue to write data until new command is received
          // opens a file or creates a file if not present,  FILE_WRITE will append data to
          // to the file created.
          dataFile = MSC.open("datalog.txt", FILE_WRITE);
          logData();
        }
        break;
      case 'x': stopLogging(); break;
      case 'd': dumpLog(); break;
      case '\r':
      case '\n':
      case 'h': menu(); break;
    }
    while (Serial.read() != -1) ; // remove rest of characters.
  } 

  if(write_data) logData();
}

void logData()
{
    // make a string for assembling the data to log:
    String dataString = "";
  
    // read three sensors and append to the string:
    for (int analogPin = 0; analogPin < 3; analogPin++) {
      int sensor = analogRead(analogPin);
      dataString += String(sensor);
      if (analogPin < 2) {
        dataString += ",";
      }
    }
  
    // if the file is available, write to it:
    if (dataFile) {
      dataFile.println(dataString);
      // print to the serial port too:
      Serial.println(dataString);
      record_count += 1;
    } else {
      // if the file isn't open, pop up an error:
      Serial.println("error opening datalog.txt");
    }
    delay(100); // run at a reasonable not-too-fast speed for testing
}

void stopLogging()
{
  Serial.println("\nStopped Logging Data!!!");
  write_data = false;
  // Closes the data file.
  dataFile.close();
  Serial.printf("Records written = %d\n", record_count);
}


void dumpLog()
{
  Serial.println("\nDumping Log!!!");
  // open the file.
  dataFile = MSC.open("datalog.txt");

  // if the file is available, write to it:
  if (dataFile) {
    while (dataFile.available()) {
      Serial.write(dataFile.read());
    }
    dataFile.close();
  }  
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog.txt");
  } 
}

void menu()
{
  Serial.println();
  Serial.println("Menu Options:");
  Serial.println("\tl - List files on disk");
  Serial.println("\te - Erase files on disk");
  Serial.println("\ts - Start Logging data (Restarting logger will append records to existing log)");
  Serial.println("\tx - Stop Logging data");
  Serial.println("\td - Dump Log");
  Serial.println("\th - Menu");
  Serial.println();
}

void listFiles()
{
  Serial.print("\n Space Used = ");
  Serial.println(MSC.usedSize());
  Serial.print("Filesystem Size = ");
  Serial.println(MSC.totalSize());

  printDirectory(MSC);
}

void eraseFiles()
{
/*  myfs.quickFormat();  // performs a quick format of the created di
  Serial.println("\nFiles erased !");
  */
}

void printDirectory(FS &fs) {
  Serial.println("Directory\n---------");
  printDirectory(fs.open("/"), 0);
  Serial.println();
}

void printDirectory(File dir, int numSpaces) {
   while(true) {
     File entry = dir.openNextFile();
     if (! entry) {
       //Serial.println("** no more files **");
       break;
     }
     printSpaces(numSpaces);
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println("/");
       printDirectory(entry, numSpaces+2);
     } else {
       // files have sizes, directories do not
       printSpaces(36 - numSpaces - strlen(entry.name()));
       Serial.print("  ");
       Serial.print(entry.size(), DEC);
       DateTimeFields datetime;
       if (entry.getModifyTime(datetime)) {
         printSpaces(4);
         printTime(datetime);
       }
       Serial.println();
     }
     entry.close();
   }
}

void printSpaces(int num) {
  for (int i=0; i < num; i++) {
    Serial.print(" ");
  }
}

void printTime(const DateTimeFields tm) {
  const char *months[12] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
  };
  if (tm.hour < 10) Serial.print('0');
  Serial.print(tm.hour);
  Serial.print(':');
  if (tm.min < 10) Serial.print('0');
  Serial.print(tm.min);
  Serial.print("  ");
  Serial.print(months[tm.mon]);
  Serial.print(" ");
  Serial.print(tm.mday);
  Serial.print(", ");
  Serial.print(tm.year + 1900);
}

