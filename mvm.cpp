/**
 * @file mvm.cpp
 * @brief Makgeolli Volume Meter System (MVM) using Arduino and Ultrasonic Sensor with OLED Display
 *
 * This code implements a makgeolli volume meter system using an Arduino and an ultrasonic sensor.
 * It allows users to view the current volume of the makgeolli tank, adjust settings, and load/save settings.
 * The system uses an OLED display to show the information and buttons for user interaction.
 *
 * @author starlight.kim
 * @version 1.0
 * @date 2021-09-30
 */

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#define OLED_RESET 4
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

#define TRIG_PIN A2
#define ECHO_PIN A3

#define BUTTON_UP_PIN 2
#define BUTTON_DOWN_PIN 3
#define BUTTON_LEFT_PIN 7
#define BUTTON_RIGHT_PIN 5
#define BUTTON_SELECT_PIN 6

#define MAIN_SCREEN 0
#define MENU_SCREEN 1
#define VIEW_SCREEN 2
#define SETTINGS_SCREEN 3
#define LOAD_SCREEN 4

#define DEBOUNCE_DELAY 100
#define LONG_PRESS_TIME 2500
#define POST_PRESS_IGNORE 10

#define PI 3.14159265359

struct WaterTankSetting
{
    float minHeight = 0;
    float diameter = 0;
    float targetCapacity = 0;
};

WaterTankSetting settings[5];
int currentSettingAddress = 5 * sizeof(WaterTankSetting);
int currentSetting = 0;
int currentScreen = MAIN_SCREEN;
int menuItem = 0;

/**
 * @brief Initializes the necessary components and settings for the program.
 *
 * This function is called once at the beginning of the program.
 * It sets up the serial communication, pin modes for buttons and sensors,
 * and initializes the display.
 */
void setup()
{
    Serial.begin(9600);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
    pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();

    EEPROM.get(currentSettingAddress, currentSetting);

    // If there is garbage value in the initial value when booting after the initial ROM write, set the initial value and save it to EEPROM
    if (currentSetting < 0 || currentSetting >= 5)
    {
        currentSetting = 0; // 초기값
        EEPROM.put(currentSettingAddress, currentSetting);
    }

    loadSettings();
}

/**
 * @brief The main loop function that runs repeatedly in the program.
 *
 * This function handles buttons, updates the display, pings the sonar, and introduces a delay of 100 milliseconds.
 */
void loop()
{
    handleButtons();
    updateDisplay();
    pingSonar();
    delay(100);
}

/**
 * @brief Handles the button inputs and performs corresponding actions based on the current screen.
 *
 * This function reads the state of the buttons and performs different actions depending on the current screen.
 * It checks if any button is pressed and updates the current screen and menu item accordingly.
 *
 * @note This function assumes that the button pins are defined and the necessary libraries are included.
 */
void handleButtons()
{
    // Read the state of the buttons
    bool up = !digitalRead(BUTTON_UP_PIN);
    bool down = !digitalRead(BUTTON_DOWN_PIN);
    bool left = !digitalRead(BUTTON_LEFT_PIN);
    bool right = !digitalRead(BUTTON_RIGHT_PIN);
    bool select = !digitalRead(BUTTON_SELECT_PIN);
    bool anyButtonPressed = up || down || select;

    switch (currentScreen)
    {
    case MAIN_SCREEN:
        if (select)
        {
            currentScreen = MENU_SCREEN;
            menuItem = -1;
        }
        break;
    case MENU_SCREEN:
        // up and down to navigate menu
        if (up)
        {
            menuItem = (menuItem + 3) % 4;
        }
        if (down)
        {
            menuItem = (menuItem + 1) % 4;
        }
        // select to execute menu action
        if (select)
        {
            executeMenuAction();
        }
        break;
    case VIEW_SCREEN:
        // select to go back to main screen
        if (select)
        {
            currentScreen = MENU_SCREEN;
        }
        break;
    case SETTINGS_SCREEN:
        // up and down to navigate menu
        if (up)
        {
            menuItem = (menuItem + 3) % 4;
        }
        if (down)
        {
            menuItem = (menuItem + 1) % 4;
        }
        if (menuItem == 0)
        {
            // select to adjust setting
            if (select)
            {
                adjustSetting(true);
            }
        }
        else if (menuItem < 3)
        {
            // select to adjust setting
            if (left)
            {
                adjustSetting(false);
            }
            else if (right)
            {
                adjustSetting(true);
            }
        }
        else
        {
            if (select)
            {
                saveSettings(currentSetting);
                currentScreen = MENU_SCREEN;
                menuItem = -1;
            }
        }
        break;
    case LOAD_SCREEN:
        // up and down to navigate menu
        if (up)
        {
            menuItem = (menuItem + 1) % 2;
        }
        if (down)
        {
            menuItem = (menuItem + 1) % 2;
        }

        if (menuItem == 0)
        {
            // select to adjust setting
            if (left)
            {
                adjustCurrentSettingIndex(false);
            }
            else if (right)
            {
                adjustCurrentSettingIndex(true);
            }
        }
        else
        {
            if (select)
            {
                EEPROM.put(currentSettingAddress, currentSetting);
                loadSettings();
                currentScreen = MENU_SCREEN;
                menuItem = -1;
            }
        }
        break;
    }
}

/**
 * @brief Executes the menu action based on the selected menu item.
 *
 * This function changes the current screen based on the selected menu item.
 */
void executeMenuAction()
{
    switch (menuItem)
    {
    case 0:
        currentScreen = MAIN_SCREEN;
        break;
    case 1:
        currentScreen = VIEW_SCREEN;
        break;
    case 2:
        currentScreen = SETTINGS_SCREEN;
        break;
    case 3:
        currentScreen = LOAD_SCREEN;
        break;
    }
    menuItem = -1;
}

/**
 * @brief Adjusts the setting based on the selected menu item.
 *
 * This function adjusts the setting based on the selected menu item.
 * It increases or decreases the setting value by 10 units.
 *
 * @param increase A boolean value that indicates whether to increase the setting value.
 */
void adjustSetting(bool increase)
{
    float adjustment = increase ? 10.0f : -10.0f;
    switch (menuItem)
    {
    case 0: // min height
        settings[currentSetting].minHeight = pingSonar();
        break;
    case 1: // diameter
        if (isnan(settings[currentSetting].diameter))
        {
            settings[currentSetting].diameter = 0;
        }

        settings[currentSetting].diameter += adjustment;

        if (settings[currentSetting].diameter < 0)
        {
            settings[currentSetting].diameter = 0;
        }
        break;
    case 2: // target capacity
        if (isnan(settings[currentSetting].targetCapacity))
        {
            settings[currentSetting].targetCapacity = 0;
        }

        settings[currentSetting].targetCapacity += adjustment;

        if (settings[currentSetting].targetCapacity < 0)
        {
            settings[currentSetting].targetCapacity = 0;
        }
        break;
    }
}

void adjustCurrentSettingIndex(bool increase)
{
    float adjustment = increase ? 1.0f : -1.0f;
    switch (menuItem)
    {
    case 0: // diameter
        if (isnan(currentSetting))
        {
            currentSetting = 0;
        }

        currentSetting += adjustment;

        if (currentSetting < 0)
        {
            currentSetting = 0;
        }

        if (currentSetting >= 5)
        {
            currentSetting = 4;
        }
        break;
    }
}

/**
 * @brief Updates the display based on the current screen.
 *
 * This function clears the display and updates the content based on the current screen.
 */
void updateDisplay()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    switch (currentScreen)
    {
    case MAIN_SCREEN:
        updateMainScreen();
        break;
    case MENU_SCREEN:
        updateMenuScreen();
        break;
    case VIEW_SCREEN:
        updateViewSettingsScreen();
        break;
    case SETTINGS_SCREEN:
        updateEditSettingsScreen();
        break;
    case LOAD_SCREEN:
        updateLoadSettingsScreen();
        break;
    }
    display.display();
}

/**
 * @brief Updates the main screen with the current volume of the water tank.
 *
 * This function calculates the volume of the water tank based on the current settings and displays it on the screen.
 */
void updateMainScreen()
{
    float distance = pingSonar();
    float height = (settings[currentSetting].minHeight - distance);
    float volume = (PI * settings[currentSetting].diameter * height) / 1000.0f; // Convert volume from cm^3 to L

    if (isnan(volume))
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("please set up tank");
    }
    else
    {
        if (volume < 0)
        {
            volume = 0;
        }

        float remainingCapacity = settings[currentSetting].targetCapacity - volume;

        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor((display.width() - 6 * 8) / 2, 15);
        display.print(volume, 1);
        display.print(" L");

        // Draw progress bar
        int progressBarWidth = display.width() - 4;
        int progressBarHeight = 8;
        int progressBarX = 2;
        int progressBarY = (display.height() - progressBarHeight) / 2 + 15;

        display.drawRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, WHITE);
        int progress = map(volume, 0, settings[currentSetting].targetCapacity, 0, progressBarWidth);
        display.fillRect(progressBarX, progressBarY, progress, progressBarHeight, WHITE);

        display.display();
    }
}

/**
 * @brief Updates the menu screen with the list of menu items.
 *
 * This function displays the list of menu items on the screen and highlights the selected menu item.
 */
void updateMenuScreen()
{
    display.setTextSize(1);
    display.setCursor((display.width() - (4 * 6)) / 2, 0);
    display.println("Menu");

    display.drawLine(0, 10, display.width(), 10, WHITE);

    const char *menuItems[] = {"Main Screen", "View Settings", "Edit Settings", "Load Settings"};

    for (int i = 0; i < 5; i++)
    {
        if (i == menuItem)
        {
            display.fillRect(0, i * 10 + 11, display.width(), 10, WHITE);
            display.setTextColor(BLACK, WHITE);
        }
        else
        {
            display.setTextColor(WHITE, BLACK);
        }
        display.setCursor(0, i * 10 + 11);
        display.println(menuItems[i]);
    }

    display.display();
}

/**
 * @brief Updates the view settings screen with the current settings.
 *
 * This function displays the current settings on the screen.
 */
void updateViewSettingsScreen()
{

    display.setTextSize(1);
    display.setCursor((display.width() - (13 * 6)) / 2, 0);
    display.println("View Settings");

    display.drawLine(0, 10, display.width(), 10, WHITE);

    display.setCursor(0, 14);

    display.print("Min Height: ");
    display.println(static_cast<int>(settings[currentSetting].minHeight));
    display.print("Diameter: ");
    display.println(static_cast<int>(settings[currentSetting].diameter));
    display.print("Target Capacity: ");
    display.println(static_cast<int>(settings[currentSetting].targetCapacity));
}

/**
 * @brief Updates the edit settings screen with the current settings.
 *
 * This function displays the current settings on the screen and allows the user to edit them.
 */
void updateEditSettingsScreen()
{
    display.setTextSize(1);
    display.setCursor((display.width() - (13 * 6)) / 2, 0);
    display.println("Edit Settings");

    display.drawLine(0, 10, display.width(), 10, WHITE);

    const char *menuItems[] = {"Min Height: ", "Diameter:", "Target Capacity:", "Save Settings"};
    float values[] = {settings[currentSetting].minHeight, settings[currentSetting].diameter, settings[currentSetting].targetCapacity};

    for (int i = 0; i < 4; i++)
    {
        if (i == menuItem)
        {
            display.fillRect(0, i * 10 + 11, display.width(), 10, WHITE);
            display.setTextColor(BLACK, WHITE);
        }
        else
        {
            display.setTextColor(WHITE, BLACK);
        }
        display.setCursor(0, i * 10 + 11);
        display.print(menuItems[i]);
        if (i < 3)
        {
            display.println(static_cast<int>(values[i]));
        }
    }

    display.display();
}

/**
 * @brief Updates the load settings screen.
 *
 * This function displays the load settings screen on the display.
 */
void updateLoadSettingsScreen()
{
    display.setTextSize(1);
    display.setCursor((display.width() - (13 * 6)) / 2, 0);
    display.println("Load Settings");

    display.drawLine(0, 10, display.width(), 10, WHITE);

    const char *menuItems[] = {"index: ", "Load Settings"};
    float values[] = {currentSetting};

    for (int i = 0; i < 2; i++)
    {
        if (i == menuItem)
        {
            display.fillRect(0, i * 10 + 11, display.width(), 10, WHITE);
            display.setTextColor(BLACK, WHITE);
        }
        else
        {
            display.setTextColor(WHITE, BLACK);
        }
        display.setCursor(0, i * 10 + 11);
        display.print(menuItems[i]);
        if (i < 1)
        {
            display.println(static_cast<int>(values[i]));
        }
    }

    display.display();
}

/**
 * @brief Pings the sonar sensor and returns the distance to the nearest object.
 *
 * This function sends a pulse to the sonar sensor and measures the time it takes for the pulse to return.
 * It then calculates the distance to the nearest object based on the speed of sound and the time taken.
 *
 * @return The distance to the nearest object in centimeters.
 */
float pingSonar()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH);
    float distance = duration * 0.034 / 2;
    return distance;
}

/**
 * @brief Loads the settings from the EEPROM memory.
 *
 * This function reads the settings from the EEPROM memory and stores them in the settings array.
 */
void loadSettings()
{
    for (int i = 0; i < 5; i++)
    {
        EEPROM.get(i * sizeof(WaterTankSetting), settings[i]);
    }
}

/**
 * @brief Saves the settings to the EEPROM memory.
 *
 * This function saves the settings to the EEPROM memory based on the given index.
 *
 * @param index The index of the settings array to save.
 */
void saveSettings(int index)
{
    if (index >= 0 && index < 5)
    {
        EEPROM.put(index * sizeof(WaterTankSetting), settings[index]);
    }
}
