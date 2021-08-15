#include <ESPmDNS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <time.h>
#include <vector> 

#define SECS_PER_DAY 86400UL

const char* ntpServer = "south-america.pool.ntp.org";
const long Offset_sec = -10800;

// Replace with your network credentials
const char* ssid = "SSID";
const char* password = "PASSWORD";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

RTC_DATA_ATTR int SamplingPeriod = 2;
RTC_DATA_ATTR int MeasurementPeriod = 60;
int NewSamplingPeriod = 2;
int NewMeasurementPeriod = 60;

const int NumberOfParameters = 3;
std::vector<time_t> datetime;
std::vector<float> AVG;
std::vector<float> STD;
double sum_voltage_1;
double sum_voltage_2;
double sum_voltage_3;
double square_voltage_1;
double square_voltage_2;
double square_voltage_3;
int16_t num_voltage_1;
int16_t num_voltage_2;
int16_t num_voltage_3;
char payload_buffer[10000];
time_t last_datetime;

double avg(double sum, int N){
  return sum/N;
}

double stdev(double sum,double square, int N){
  return sqrt((square-pow(sum,2)/N)/(N-1));
}

bool get_time_right_now()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return 0;
  }
  last_datetime = mktime(&timeinfo);
  return 1;
}

String utc_time()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "nan";
  }
  time_t trn_t = mktime(&timeinfo);
  return time_t2datetime(trn_t);
}

String time_t2datetime(time_t trn_t)
{
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&trn_t));
  return buffer;
}

String time_t2date(time_t trn_t)
{
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", localtime(&trn_t));
  return buffer;
}

void serialOutFlush()
{
  Serial.flush();
}

void serialIncFlush()
{
  while(Serial.available()) Serial.read();
}

void setup(){
  // Serial port for debugging purposes
  Serial.begin(115200);
  serialIncFlush();
  serialOutFlush();
  pinMode(32,INPUT);
  pinMode(33,INPUT);
  pinMode(34,INPUT);

  // Initialize SPIFFS
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }

  if(!MDNS.begin("datalogger")) {
     Serial.println("Error starting mDNS");
     return;
  }

  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html");
  });
  server.on("/voltagePinA", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", payload(1));
  });
  server.on("/voltagePinB", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", payload(2));
  });
  server.on("/voltagePinC", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", payload(3));
  });
  server.on("/slow", HTTP_GET, [] (AsyncWebServerRequest *request) {
    NewSamplingPeriod = 5;
    NewMeasurementPeriod = 600;
    request->send(200, "text/plain", "Set to slow mode.");
  });
  server.on("/quick", HTTP_GET, [] (AsyncWebServerRequest *request) {
    NewSamplingPeriod = 2;
    NewMeasurementPeriod = 60;
    request->send(200, "text/plain", "Set to quick mode. (Mostly for debugging.)");
  });

  configTime(Offset_sec, 0, ntpServer);
  delay(1000);
  time(&last_datetime);
  Serial.println("Time of internal RTC set to \"" + time_t2datetime(last_datetime) + "\".");
  
  // Start server
  server.begin();
}
 
void loop(){
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.begin(ssid,password);
    // remove old files in order not to fill up the flash memory
    SPIFFS.remove("/Data_"+time_t2date(last_datetime-2*SECS_PER_DAY)+".csv");
  }
  num_voltage_1 = 0;
  num_voltage_2 = 0;
  num_voltage_3 = 0;
  sum_voltage_1 = 0;
  sum_voltage_2 = 0;
  sum_voltage_3 = 0;
  square_voltage_1 = 0;
  square_voltage_2 = 0;
  square_voltage_3 = 0;
  for(int i=0; i < MeasurementPeriod/(2*SamplingPeriod); i++){
    measure_all();
  }
  time(&last_datetime);
  for(int i=0; i < MeasurementPeriod/(2*SamplingPeriod); i++){
    measure_all();
  }
  String filename_now = "/Data_" + time_t2date(last_datetime) + ".csv";
  File file_test = SPIFFS.open(filename_now.c_str());
  bool file_exists = file_test;
  file_test.close();
  if(!file_exists) {
    Serial.println("File doens't exist");
    Serial.println("Creating file \"" + filename_now + "\".");
    writeFile(SPIFFS, filename_now.c_str(), "Datetime,AVG_voltage1,STD_voltage1,AVG_voltage2,STD_voltage2,AVG_voltage3,STD_voltage3\r\n");
  }
  String dataMessage = "\r\n" + time_t2datetime(last_datetime) + "," + String(avg(sum_voltage_1,num_voltage_1),3) + "," + String(stdev(sum_voltage_1,square_voltage_1,num_voltage_1),3) + "," + String(avg(sum_voltage_2,num_voltage_2),3) + "," + String(stdev(sum_voltage_2,square_voltage_2,num_voltage_2),3) + "," + String(avg(sum_voltage_3,num_voltage_3),3) + "," + String(stdev(sum_voltage_3,square_voltage_3,num_voltage_3),3);
  appendFile(SPIFFS, filename_now.c_str(), dataMessage.c_str());
  SamplingPeriod = NewSamplingPeriod;
  MeasurementPeriod = NewMeasurementPeriod;
}

char* payload(int parameter)
{ 
  loadData(SPIFFS,last_datetime,parameter);
  strcpy(payload_buffer,"{\"Data\":[");
  int number = min((int) AVG.size(),6*24);
  int offset = AVG.size()-number;
  int digits = 3;
  for(int i = 0; i < number; i++)
  {
    if(i > 0){
      strcat(payload_buffer,",");
    }
    String temp = "[" + String(offset+i) + ",\"" + time_t2datetime(datetime[offset+i]) + "\",\"" + String(AVG[offset+i],digits) + "\",\"" + String(STD[offset+i],digits) + "\"]";
    strcat(payload_buffer,temp.c_str());
  }
  strcat(payload_buffer,"]}");
  return payload_buffer;
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)) {
    Serial.print("Message appended:");
    Serial.println(message);
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void loadData(fs::FS &fs, time_t until_this_datetime, int parameter) {
  File this_day = fs.open("/Data_"+time_t2date(until_this_datetime)+".csv", FILE_READ);
  File preceding_day = fs.open("/Data_"+time_t2date(until_this_datetime-SECS_PER_DAY)+".csv", FILE_READ);
  datetime.clear();
  AVG.clear();
  STD.clear();
  bool firstLine = true;
  while (preceding_day.available()) {
    int c;
    if (firstLine) {
      c = preceding_day.read();
      if (c == '\n') {
        firstLine = false;
      }
    } 
    else {
      String proto = preceding_day.readStringUntil(',');
      proto.trim();
      struct tm read_time;
      strptime(proto.c_str(),"%Y-%m-%d %H:%M:%S",&read_time);
      datetime.push_back(mktime(&read_time));
      if(parameter > 1)
      {
        for(int i = 1; i < parameter; i++)
        {
          preceding_day.readStringUntil(',');
          preceding_day.readStringUntil(',');
        }
      }
      char peek_buffer[16];
      preceding_day.readBytesUntil(',', peek_buffer, 16);
      if(strcmp(peek_buffer, "nan")  == 0){
        datetime.pop_back();
        preceding_day.readStringUntil('\n');
      }
      else{
        AVG.push_back(atof(peek_buffer));
        STD.push_back(preceding_day.parseFloat());
        preceding_day.readStringUntil('\n');
      }
      memset(peek_buffer, 0, sizeof(peek_buffer));
    }
  }
  preceding_day.close();
  firstLine = true;
  while (this_day.available()) {
    int c;
    if (firstLine) {
      c = this_day.read();
      if (c == '\n') {
        firstLine = false;
      }
    } else {
      String proto = this_day.readStringUntil(',');
      proto.trim();
      struct tm read_time;
      strptime(proto.c_str(),"%Y-%m-%d %H:%M:%S",&read_time);
      datetime.push_back(mktime(&read_time));
      if(parameter > 1)
      {
        for(int i = 1; i < parameter; i++)
        {
          this_day.readStringUntil(',');
          this_day.readStringUntil(',');
        }
      }
      char peek_buffer[16];
      this_day.readBytesUntil(',', peek_buffer, 16);
      if(strcmp(peek_buffer, "nan")  == 0){
        datetime.pop_back();
        this_day.readStringUntil('\n');
      }
      else{
        AVG.push_back(atof(peek_buffer));
        STD.push_back(this_day.parseFloat());
        this_day.readStringUntil('\n');
      }
      memset(peek_buffer, 0, sizeof(peek_buffer));
    }
  }
  this_day.close();
}

void measure_all(){
  // Carry out your measurements here. For simplicity, we use the internal ADC for demonstration.
  long t1 = millis();
  int adc1, adc2, adc3;
  adc1 = analogRead(32);
  delay(100);
  adc2 = analogRead(33);
  delay(100);
  adc3 = analogRead(34);
  sum_voltage_1 += adc1*0.000806;
  square_voltage_1 += pow(adc1*0.000806,2);
  num_voltage_1++;
  sum_voltage_2 += adc2*0.000806;
  square_voltage_2 += pow(adc2*0.000806,2);
  num_voltage_2++;
  sum_voltage_3 += adc3*0.000806;
  square_voltage_3 += pow(adc3*0.000806,2);
  num_voltage_3++;
  Serial.println("ADC: " + String(adc1) + " , " + String(adc2) + " , " + String(adc3));
  long t2 = millis();
  if(SamplingPeriod*1000-(millis()-t1) > 0) delay(SamplingPeriod*1000-(t2-t1));
}
