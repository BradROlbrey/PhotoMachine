
#include <TMCStepper.h>
#include <AccelStepper.h>
#include <Wire.h>
#include <util/atomic.h> // this library includes the ATOMIC_BLOCK macro.


/*
 *  TMC2208 Driver
 */
#define SW_RX  12  // SoftwareSerial receive pin
#define SW_TX  11  // SoftwareSerial transmit pin
#define R_SENSE 0.11f 
TMC2208Stepper driver(SW_RX, SW_TX, R_SENSE);
// Seeing as how I pass those in as parameters, I could probably control two different drivers
// easily enough by just having two instances, esp since it's software serial.

#define MICROSTEPS 256L  // Don't do zero; I changed the library with the first program that has this comment.

/*
 *  AccelStepper
 */
AccelStepper *stepper_arm;  // Need step_forw to be initialized first, so wait until setup() to instantiate an instance.
AccelStepper *stepper_line;

#define ENA_PIN             7  // For both motors

#define ARM_DIR_PIN         5   // Direction
#define ARM_STEP_PIN        6   // Step

#define LINE_DIR_PIN        8
#define LINE_STEP_PIN       9


/*
 *  Photo stuff
 */
// Don't think these need to be volatile, but just in case.
volatile int num_photos_per_rev = NULL;  // Number of pictures to take all the way around the object.
volatile int num_levels = NULL;  // Number of rotations to make, number of times camera moves up.
  
long steps_per_photo_around;  // To be assigned after receiving photos per rotation
  // (steps / rev) / (photos / rev) => (steps / photo)
  // May not always divide cleanly, but it's good enough with our epic microstepping, and we'll
  //  be switching direction anyways, so not going to do type double for now.
long steps_per_level;

bool arm_dir = false;
bool line_dir = false;

/*
 *  I2C
 */
volatile char next_byte = ' ';  // Stores the value from Wire.read().
volatile byte moving = 0;

/*
 *  Serial.printing
 */
unsigned long prev_time;
unsigned int print_interval;


void setup() {
  Serial.begin(9600);

  /*
   *  TMC2208 Driver
   */
  driver.beginSerial(9600);     // SW UART drivers

  driver.begin();                 // SPI: Init CS pins and possible SW SPI pins
                                  // UART: Init SW UART (if selected) with default 115200 baudrate
  driver.toff(5);                 // Enables driver in software
  driver.microsteps(MICROSTEPS);  // Set microsteps to whatever

  driver.I_scale_analog(1);
  driver.internal_Rsense(0);
  //driver.intpol(0);
  driver.ihold(31);
  driver.irun(31);
  driver.iholddelay(15);

//driver.en_spreadCycle(false);   // Toggle spreadCycle on TMC2208
  driver.pwm_autoscale(true);     // Needed for stealthChop


  /*
   *  AccelStepper
   */
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, HIGH);  // HIGH is disabled
  
  stepper_arm = new AccelStepper(step_arm, step_back_arm);
  stepper_arm->setMaxSpeed(50L * MICROSTEPS);
  stepper_arm->setAcceleration(50L * MICROSTEPS);
  
  pinMode(ARM_STEP_PIN, OUTPUT);
  digitalWrite(ARM_STEP_PIN, LOW);
  pinMode(ARM_DIR_PIN, OUTPUT);
  digitalWrite(ARM_DIR_PIN, LOW);
  
  stepper_line = new AccelStepper(step_line, step_back_line);
  stepper_line->setMaxSpeed(100L * MICROSTEPS);
  stepper_line->setAcceleration(100L * MICROSTEPS);
  
  pinMode(LINE_STEP_PIN, OUTPUT);
  digitalWrite(LINE_STEP_PIN, LOW);
  pinMode(LINE_DIR_PIN, OUTPUT);
  digitalWrite(LINE_DIR_PIN, LOW);


  /*
   *  I2C
   */
  Wire.begin(0x7f);               // Join i2c bus with address
  Wire.onReceive(receiveEvent);   // Register event for receiving data from master
  Wire.onRequest(requestEvent);   // Register event for sending data to master upon request

  /*
   *  Serial
   */
  //while (!Serial);  // Wait for the Serial monitor, apparently it's s/w instead of h/w
  //Serial.println("Hello, World!");  // so it takes longer to initialize.

  /*
   *  Photo stuff
   */
  const long TOTAL_STEPS_AROUND = 200L * MICROSTEPS;
  const long TOTAL_STEPS_UP = 300L * MICROSTEPS;
  
  print_interval = 5000;
  prev_time = millis() - print_interval;
  while (num_photos_per_rev == NULL ||
         num_levels == NULL) {
    if (millis() - prev_time > print_interval) {
      Serial.println("Waiting to receive num_photos and num_levels");
      prev_time += print_interval;
    }
  }
  print_interval = 250;
  
  Serial.println();
  
  steps_per_photo_around = TOTAL_STEPS_AROUND / num_photos_per_rev;
  steps_per_level = TOTAL_STEPS_UP / (num_levels - 1);
  
  Serial.print("Pictures per rotation: ");
  Serial.println(num_photos_per_rev);
  Serial.print("Number of levels: ");
  Serial.println(num_levels);
  Serial.print("Total number of pictures to take: ");
  Serial.println(num_photos_per_rev * num_levels);

  Serial.println();
}

void loop() {
  driver.microsteps(MICROSTEPS);  // Because I keep powering the drivers after the arduino...

  switch (next_byte) {
    case ' ': break;
      
    case 'd':  // Rotate arm clockwise
      Serial.println('d');
      digitalWrite(ARM_DIR_PIN, LOW);
      stepper_arm->move(steps_per_photo_around);
      stepper_arm->runToPosition();  // Blocks here until motor fully moved.
      break;
      
    case 'w':  // Slide camera up
      Serial.println('w');
      digitalWrite(LINE_DIR_PIN, HIGH);
      stepper_line->move(steps_per_level);
      stepper_line->runToPosition();
      break;
      
    case 'a':  // Rotate arm counter-clockwise
      Serial.println('a');
      digitalWrite(ARM_DIR_PIN, HIGH);
      stepper_arm->move(steps_per_photo_around * (num_photos_per_rev - 1));
      stepper_arm->runToPosition();
      break;
      
    case 's':  // Slide camera down
      Serial.println('s');
      digitalWrite(LINE_DIR_PIN, LOW);
      stepper_line->move(steps_per_level * (num_levels - 1));
      stepper_line->runToPosition();
      break;

    case 'e':  // Enable motors
      Serial.println('e');
      digitalWrite(ENA_PIN, LOW);
      break;
      
    case 'q':  // Disable motors
      Serial.println('q');
      digitalWrite(ENA_PIN, HIGH);
      break;
      
    default:
      Serial.print("Invalid input");
  }
  next_byte = ' ';  // Clear next_byte so we stop moving, or clear invalid input.
  moving = 0;       // Won't get interrupted as long as moving = 1.
}


// Receive data from Pi
void receiveEvent(int howMany) {
  if (howMany == 1) {
    // Receiving the usual wasd or qe
    next_byte = Wire.read();
    Serial.print("Rec: ");
    Serial.println(next_byte);
    moving = 1;
  }
  else {
    // Receiving two important setup integers (actually they're bytes, so we can't do more than
    //  255? or 127? around or about, but that can be expanded some other time if necessary,
    //  perhaps by bitshifting multiple bytes in).
    int dont_care = Wire.read();
    num_photos_per_rev = Wire.read();
    num_levels = Wire.read();
  }
}
// Send data to Pi
void requestEvent() {
  delayMicroseconds(8);
  Wire.write(moving);
}


void step_arm() {
  digitalWrite(ARM_STEP_PIN, HIGH);
  digitalWrite(ARM_STEP_PIN, LOW);
}
void step_back_arm() { /* Just reverse direction and call step_arm() */ }

void step_line() {
  digitalWrite(LINE_STEP_PIN, HIGH);
  digitalWrite(LINE_STEP_PIN, LOW);
}
void step_back_line() { }
