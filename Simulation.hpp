#pragma once
#include <iostream>
#include <vector>
#include <queue>
#include <random>
#include <cmath>
#include <map>
#include <memory>
#include <algorithm>
#include <string>
#include <cstdio>
#include <cassert>
#include <deque>

//------------------------------------------------------------------------------
// Common simulation constants and helper functions
//------------------------------------------------------------------------------

static const double SERVICE_RATE = 1.0 / (40.0 / 60.0); // Example: average 40 minutes per service

// Get arrival rate (λ) as function of time (can be time-dependent or constant)
double getArrivalRate(double timeHours);

// Priority levels
enum class Priority {
    CORPORATE, // 0 — highest
    PREMIUM,   // 1 — medium
    FREE       // 2 — lowest
};

// Helper for formatting hours into HH:MM
std::string formatTime(double simulationTimeHours);

//------------------------------------------------------------------------------
// Request class (describes a single request)
//------------------------------------------------------------------------------

class Request {
private:
    int id_;
    Priority priority_;
    double arrivalTime_;
    int sourceIndex_;
    double bufferEnterTime_;
    double startServiceTime_;

public:
    // Constructor to initialize a request with id, priority, arrival time, and source index
    Request(int id, Priority priority, double arrivalTime, int sourceIndex);

    // Getters for request properties
    int getId() const;
    Priority getPriority() const;
    double getArrivalTime() const;
    int getSourceIndex() const;

    // Setters and getters for buffer enter time and start service time
    void setBufferEnterTime(double t);
    double getBufferEnterTime() const;

    void setStartServiceTime(double t);
    double getStartServiceTime() const;
};

// Forward declaration of Controller
class Controller;

//------------------------------------------------------------------------------
// Buffer class (fixed-size queue with priority-based insertion/eviction)
//------------------------------------------------------------------------------

static const int BUFFER_SIZE = 8;

class Buffer {
private:
    std::deque<std::shared_ptr<Request>> requests_;

public:
    explicit Buffer(Controller* controller);
    Controller* controller_;

    // Add a request to the buffer with priority-based insertion
    bool addRequest(const std::shared_ptr<Request>& req);
    // Pop a request from the front of the buffer
    std::shared_ptr<Request> popRequest();
    // Check if the buffer is empty
    bool isEmpty() const;
};

//------------------------------------------------------------------------------
// Two event types in the controller
//------------------------------------------------------------------------------

enum class EventType {
    REQUEST_GENERATED, // A new request arrived (generated)
    REQUEST_SERVED     // A request finished service
};

// Event structure
struct Event {
    EventType type;
    double time;
    std::shared_ptr<Request> request;
    int deviceId; // used for REQUEST_SERVED to indicate which device

    bool operator<(const Event& other) const {
        // We want the earliest event first, so invert comparison
        return time > other.time;
    }
};

//------------------------------------------------------------------------------
// Device class (each device processes requests one at a time)
//------------------------------------------------------------------------------

class Device {
private:
    int id_;
    bool busy_;
    double finishTime_;
    double busyTotalTime_;
    double startBusyTime_;
    std::shared_ptr<Request> currentRequest_;
    double serviceTimeHours_;  // to store the generated service time
    std::mt19937 rng_;

public:
    explicit Device(int id);

    // Check if the device is busy
    bool isBusy() const;
    // Get the finish time of the current request
    double getFinishTime() const;
    // Get the total busy time of the device
    double getBusyTotalTime() const;

    // Load a request onto the device and generate service time
    void loadRequest(const std::shared_ptr<Request>& req, double currentTimeHours);
    // Free the device after completing a request
    void freeDevice(double timeHours);

    // Get the generated service time in hours
    double getServiceTimeHours() const;
    int getId() const;
};

//------------------------------------------------------------------------------
// Source class (generates requests of a given priority)
//------------------------------------------------------------------------------

class Source {
private:
    Priority priority_;
    int sourceIndex_;
    std::mt19937 rng_;

public:
    Source(Priority priority, int sourceIndex);

    // Generate the inter-arrival time for the next request
    double generateInterArrivalTime(double currentTimeHours);
    // Create a new request
    std::shared_ptr<Request> createRequest(int requestId, double arrivalTimeHours);

    // Schedule the next request generation
    void scheduleNextRequest(Controller& controller, double currentTime);

    Priority getPriority() const;
    int getSourceIndex() const;
};

//------------------------------------------------------------------------------
// Controller class (manages the simulation events and overall logic)
//------------------------------------------------------------------------------

class Controller {
private:
    std::priority_queue<Event> events_;
    std::vector<std::unique_ptr<Source>> sources_;
    std::vector<std::unique_ptr<Device>> devices_;

    Buffer buffer_;

    std::mt19937 rng_;
    int globalRequestId_;

    const int maxRequests_;
    int generatedRequestsCount_;

    int rejectedRequests_;
    std::map<Priority, int> rejectedByPriority_;

    double totalWaitTime_;
    int servedRequestsCount_;
    double lastEventTime_;

public:
    Controller(int numCorporate, int numPremium, int numFree,
        int numDevices, int maxRequests);

    // Initialize the first requests for each source
    void initRequests();
    // Main simulation loop
    void work();
    // Print final statistics
    void printStatistics();

    // Increment the number of rejected requests
    void incrementRejectedRequests();
    // Increment the number of rejected requests by priority
    void incrementRejectedByPriority(Priority pr);

    int getRejectedByPriority(Priority p) const;
    int getServedRequestsCount() const;

    // Event queue management
    void pushEvent(const Event& ev);
    bool eventsEmpty() const;
    Event popEvent();

    // Request counters
    int& getGlobalRequestIdRef();
    int getGlobalRequestId() const;
    int& getGeneratedRequestsCountRef();
    int getMaxRequests() const;

    // Access to devices and sources
    std::vector<std::unique_ptr<Device>>& getDevices();
    std::vector<std::unique_ptr<Source>>& getSources();

    // Load requests from buffer to free devices
    void loadRequestsToFreeDevices(double currentTime);

    // Update the time of the last event
    void updateLastEventTime(double t);

    // Handle the completion of a request
    void handleRequestFinished(int deviceId, double currentTime, const std::shared_ptr<Request>& req);

    // Handle a newly generated request
    void handleRequestGenerated(const std::shared_ptr<Request>& req, double currentTime);
};