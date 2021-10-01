#include "Arduino.h"
#include "mscFS.h"
#include "sdios.h"

// Serial output stream
ArduinoOutStream cout(Serial);

// Setup USBHost_t36 and as many HUB ports as needed.
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);

// Setup MSC for the number of USB Drives you are using. (Two for this example)
// Mutiple  USB drives can be used. Hot plugging is supported. There is a slight
// delay after a USB MSC device is plugged in. This is waiting for initialization
// but after it is initialized ther should be no delay.
#define CNT_PARITIONS 10 
PFsVolume partVols[CNT_PARITIONS];
uint8_t partVols_drive_index[CNT_PARITIONS];
uint8_t count_partVols = 0;
  
#define CNT_MSDRIVES 3
msController msDrives[CNT_MSDRIVES](myusb);
UsbFs msc[CNT_MSDRIVES];
bool g_exfat_dump_changed_sectors = false;
uint32_t log_partVol_index;

PFsLib pfsLIB;

uint8_t  sectorBuffer[512];
uint8_t volName[32];
PFsFile dataFile;
int record_count = 0;
bool write_data = false;

//----------------------------------------------------------------
void dump_hexbytes(const void *ptr, int len)
{
  if (ptr == NULL || len <= 0) return;
  const uint8_t *p = (const uint8_t *)ptr;
  while (len) {
    for (uint8_t i = 0; i < 32; i++) {
      if (i > len) break;
      Serial.printf("%02X ", p[i]);
    }
    Serial.print(":");
    for (uint8_t i = 0; i < 32; i++) {
      if (i > len) break;
      Serial.printf("%c", ((p[i] >= ' ') && (p[i] <= '~')) ? p[i] : '.');
    }
    Serial.println();
    p += 32;
    len -= 32;
  }
}
//----------------------------------------------------------------
// Function to handle one MS Drive...
void processMSDrive(uint8_t drive_number, msController &msDrive, UsbFs &msc)
{
  Serial.printf("Initialize USB drive...");
  //cmsReport = 0;
  if (!msc.begin(&msDrive)) {
    Serial.println("");
    msc.errorPrint(&Serial);
    Serial.printf("initialization drive %u failed.\n", drive_number);

    // see if we can read capacity of this device
    msSCSICapacity_t mscap;
    uint8_t status = msDrive.msReadDeviceCapacity(&mscap);
    Serial.printf("Read Capacity: status: %u Blocks: %u block Size: %u\n", status, mscap.Blocks, mscap.BlockSize);
    Serial.printf("From object: Blocks: %u Size: %u\n", msDrive.msCapacity.Blocks, msDrive.msCapacity.BlockSize);
    return;
  }
  Serial.printf("Blocks: %u Size: %u\n", msDrive.msCapacity.Blocks, msDrive.msCapacity.BlockSize);
  pfsLIB.mbrDmp( msc.usbDrive(),  msDrive.msCapacity.Blocks, Serial);

  // lets see if we have any partitions to add to our list...
  for (uint8_t i = 0; i < 4; i++) {
    if (count_partVols == CNT_PARITIONS) return; // don't overrun
    if (partVols[count_partVols].begin((USBMSCDevice*)msc.usbDrive(), true, i + 1)) {
      Serial.printf("drive %u Partition %u valid:%u\n", drive_number, i);
      partVols_drive_index[count_partVols] = drive_number;

      count_partVols++;
    }
  }
}

void dateTime(uint16_t *date, uint16_t *time, uint8_t *ms10) {
  uint32_t now = Teensy3Clock.get();
  if (now < 315532800) { // before 1980
    *date = 0;
    *time = 0;
  } else {
    DateTimeFields datetime;
    breakTime(now, datetime);
    *date = FS_DATE(datetime.year + 1900, datetime.mon + 1, datetime.mday);
    *time = FS_TIME(datetime.hour, datetime.min, datetime.sec);
  }
}

void ShowPartitionList() {
  Serial.println("\n***** Partition List *****");
  char volName[32];
  for (uint8_t i = 0; i < count_partVols; i++)  {
    Serial.printf("%d(%u:%u):>> ", i, partVols_drive_index[i], partVols[i].part());
    switch (partVols[i].fatType())
    {
    case FAT_TYPE_FAT12: Serial.printf("Fat12: "); break;
    case FAT_TYPE_FAT16: Serial.printf("Fat16: "); break;
    case FAT_TYPE_FAT32: Serial.printf("Fat32: "); break;
    case FAT_TYPE_EXFAT: Serial.printf("ExFat: "); break;
    }
    if (partVols[i].getVolumeLabel(volName, sizeof(volName))) {
      Serial.printf("Volume name:(%s)", volName);
    }
    elapsedMicros em_sizes = 0;
    uint32_t free_cluster_count = partVols[i].freeClusterCount();
    uint64_t used_size =  (uint64_t)(partVols[i].clusterCount() - free_cluster_count)
                          * (uint64_t)partVols[i].bytesPerCluster();
    uint64_t total_size = (uint64_t)partVols[i].clusterCount() * (uint64_t)partVols[i].bytesPerCluster();
    Serial.printf(" Partition Total Size:%llu Used:%llu time us: %u\n", total_size, used_size, (uint32_t)em_sizes);
  }
}


//=============================================================================
void setup(){
  Serial.begin(9600);
  while (!Serial) {
    SysCall::yield(); // wait for serial port to connect.
  }


  Serial.print(CrashReport);
  Serial.println("\n" __FILE__ " " __DATE__ " " __TIME__);
  delay(3000);
  
  // Start USBHost_t36, HUB(s) and USB devices.
  myusb.begin();


  cout << F(
         "\n"
         "Cards up to 2 GiB (GiB = 2^30 bytes) will be formated FAT16.\n"
         "Cards larger than 2 GiB and up to 32 GiB will be formatted\n"
         "FAT32. Cards larger than 32 GiB will be formatted exFAT.\n"
         "\n");

  count_partVols = 0;
  if (!msDrives[0]) {
    Serial.println("Waiting up to 5 seconds for a USB drive ");
    elapsedMillis em = 0;
    while (em < 5000) {
      myusb.Task();
      for (uint8_t i = 0; i < CNT_MSDRIVES; i++) if (msDrives[i]) break;
    }
  }
  for (uint8_t i = 0; i < CNT_MSDRIVES; i++) {
    if (msDrives[i]) {
      processMSDrive(i, msDrives[i], msc[i]);
    }    
  }

  ShowPartitionList();
  ShowCommandList();

  FsDateTime::setCallback(dateTime);

}

//=============================================================================
void ShowCommandList() {
  Serial.println("Commands:");
  Serial.println("  1 - Scan for new drive(s)");
  Serial.println("  2 - Show Partition List");
  Serial.println("=============  Logging  =============");
  Serial.println("  s <partition> - Start Logging data (Restarting logger will append records to existing log)");
  Serial.println("  x - Stop Logging data");
  Serial.println("  d <partition> - Dump Log");
  Serial.println("============ Drive Operations ===============");
  Serial.println("  f <partition> [16|32|ex] - to format");
  Serial.println("  v <partition> <label> - to change volume label");
  Serial.println("  p <partition> - print Partition info");
  Serial.println("  l <partition> - to do ls command on that partition");
  Serial.println("  c -  toggle on/off format show changed data"); 
  Serial.println("Enter command:");
}

//=============================================================================
uint32_t CommandLineReadNextNumber(int &ch, uint32_t default_num) {
  while (ch == ' ') ch = Serial.read();
  if ((ch < '0') || (ch > '9')) return default_num;

  uint32_t return_value = 0;
  while ((ch >= '0') && (ch <= '9')) {
    return_value = return_value * 10 + ch - '0';
    ch = Serial.read(); 
  }
  return return_value;  
}

//=============================================================================
void loop() {

  myusb.Task();

  if ( Serial.available() ) {
    uint8_t commmand = Serial.read();
  
    int ch = Serial.read();
    uint32_t partVol_index = CommandLineReadNextNumber(ch, 0);
    while (ch == ' ') ch = Serial.read();
    uint8_t fat_type = 0;
    switch(commmand) {
      default:
        ShowCommandList();
        break;
      case '1':
        count_partVols = 0;
        if (!msDrives[0]) {
          Serial.println("Waiting up to 5 seconds for a USB drive ");
          elapsedMillis em = 0;
          while (em < 5000) {
            myusb.Task();
            for (uint8_t i = 0; i < CNT_MSDRIVES; i++) if (msDrives[i]) break;
          }
        }
        for (uint8_t i = 0; i < CNT_MSDRIVES; i++) {
          if (msDrives[i]) {
            processMSDrive(i, msDrives[i], msc[i]);
          }    
        }
        ShowPartitionList();
        break;    
      case '2':
        ShowPartitionList();
        break;
      case 'f':
        // if there is an optional parameter try it... 
        switch(ch) {
          case '1': fat_type = FAT_TYPE_FAT16; break;
          case '3': fat_type = FAT_TYPE_FAT32; break;
          case 'e': fat_type = FAT_TYPE_EXFAT; break;
        }
  
        Serial.printf("\n **** Start format partition %d ****\n", partVol_index);
        if (partVol_index < count_partVols) 
          pfsLIB.formatter(partVols[partVol_index], fat_type, false, g_exfat_dump_changed_sectors, Serial); 
        break;
      case 'p':
        while (partVol_index < count_partVols) {
          Serial.printf("\n **** print partition info %d ****\n", partVol_index);
          pfsLIB.print_partion_info(partVols[partVol_index], Serial); 
          partVol_index = (uint8_t)CommandLineReadNextNumber(ch, 0xff);
        }
        break;
      case 'v':
        {
        if (partVol_index < count_partVols) {
            char new_volume_name[30];
            int ii = 0;
            while (ch > ' ') {
              new_volume_name[ii++] = ch;
              if (ii == (sizeof(new_volume_name)-1)) break;
              ch = Serial.read();
            }
            new_volume_name[ii] = 0;
            Serial.printf("Try setting partition index %u to %s - ", partVol_index, new_volume_name);
            if (partVols[partVol_index].setVolumeLabel(new_volume_name)) Serial.println("*** Succeeded ***");
            else Serial.println("*** failed ***");
          }
        }
        break;
      case 'c': 
        g_exfat_dump_changed_sectors = !g_exfat_dump_changed_sectors; 
        break;
      case 'l':
        Serial.printf("\n **** List fillScreen partition %d ****\n", partVol_index);
        if (partVol_index < count_partVols) 
          partVols[partVol_index].ls(LS_SIZE | LS_DATE);
        break;
      case 's':
        {
          Serial.println("\nLogging Data!!!");
          write_data = true;   // sets flag to continue to write data until new command is received
          // opens a file or creates a file if not present,  FILE_WRITE will append data to
          // to the file created.
          log_partVol_index = partVol_index;
          if (log_partVol_index < count_partVols) {
            dataFile = partVols[log_partVol_index].open("datalog.txt",  O_RDWR | O_CREAT | O_AT_END);
          } else {
            Serial.println("Part Vol Index out of Range");
            break;
          }
          logData();
        }
        break;
      case 'x': stopLogging();
        break;
      case 'd': dumpLog();
        break;
    }
  
    while (Serial.read() == -1);
    while (Serial.read() != -1);
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
  dataFile.flush(); //flush to set modified date.
  dataFile.close();
  Serial.printf("Records written = %d\n", record_count);
}


void dumpLog()
{
  Serial.println("\nDumping Log!!!");
  // open the file.
  if (log_partVol_index < count_partVols) {
    dataFile = partVols[log_partVol_index].open("datalog.txt",   O_RDWR );
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
  } else {
    Serial.println("Part Vol Index out of Range");
  }

}