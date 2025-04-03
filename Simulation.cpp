#include "simulation.hpp"

// Arrival rate function
double getArrivalRate(double timeHours) {
    // Example time-dependent: oscillation with a peak around midday or something similar
    const double period = 24.0;    // 24-hour cycle
    const double offset = 0.45;    // average rate
    const double amplitude = 0.25; // half the difference from 0.2..0.7

    double phase = 2.0 * 3.14159 * (timeHours / period);
    double rate = offset + amplitude * std::sin(phase);

    // Ensure it's positive
    if (rate <= 0) {
        rate = 0.01; // fallback to a small positive number
    }
    return rate;
}

// Time formatter
std::string formatTime(double simulationTimeHours) {
    int totalMinutes = static_cast<int>(std::floor(simulationTimeHours * 60.0));
    int hours = (totalMinutes / 60) % 24;
    int minutes = totalMinutes % 60;

    char buffer[6];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hours, minutes);
    return std::string(buffer);
}

//------------------------------------------------------------------------------
// Request class
//------------------------------------------------------------------------------
Request::Request(int id, Priority priority, double arrivalTime, int sourceIndex)
    : id_(id),
    priority_(priority),
    arrivalTime_(arrivalTime),
    sourceIndex_(sourceIndex),
    bufferEnterTime_(arrivalTime),
    startServiceTime_(0.0)
{
}

int Request::getId() const {
    return id_;
}

Priority Request::getPriority() const {
    return priority_;
}

double Request::getArrivalTime() const {
    return arrivalTime_;
}

int Request::getSourceIndex() const {
    return sourceIndex_;
}

void Request::setBufferEnterTime(double t) {
    bufferEnterTime_ = t;
}

double Request::getBufferEnterTime() const {
    return bufferEnterTime_;
}

void Request::setStartServiceTime(double t) {
    startServiceTime_ = t;
}

double Request::getStartServiceTime() const {
    return startServiceTime_;
}

//------------------------------------------------------------------------------
// Buffer class
//------------------------------------------------------------------------------
Buffer::Buffer(Controller* controller)
    : controller_(controller)
{
}

// Add a request to the buffer with priority-based insertion
bool Buffer::addRequest(const std::shared_ptr<Request>& req) {
    // If there's space in the buffer, insert by priority
    if (requests_.size() < BUFFER_SIZE) {
        auto it = std::find_if(requests_.begin(), requests_.end(),
            [&req](const auto& r) {
                // We insert before the first request that has strictly lower priority
                // or the same priority but arrived later
                if (static_cast<int>(r->getPriority()) < static_cast<int>(req->getPriority())) {
                    return false;
                }
                if (r->getPriority() == req->getPriority() && r->getArrivalTime() < req->getArrivalTime()) {
                    return false;
                }
                return true;
            }
        );
        requests_.insert(it, req);
        req->setBufferEnterTime(req->getArrivalTime());
        std::cout << "Request " << req->getId()
            << " added to buffer (priority "
            << static_cast<int>(req->getPriority()) << ").\n";
        return true;
    }

    // Buffer is full, check if we can evict a lower-priority request
    Priority newPr = req->getPriority();
    if (newPr == Priority::FREE) {
        // No chance to preempt anything
        controller_->incrementRejectedRequests();
        controller_->incrementRejectedByPriority(newPr);
        return false;
    }

    if (newPr == Priority::PREMIUM) {
        // Try to remove a FREE
        auto itFree = std::find_if(requests_.begin(), requests_.end(),
            [](auto& r) { return r->getPriority() == Priority::FREE; });
        if (itFree != requests_.end()) {
            auto evictedReq = *itFree;
            requests_.erase(itFree);
            controller_->incrementRejectedRequests();
            controller_->incrementRejectedByPriority(evictedReq->getPriority());

            std::cout << "Evicting FREE request " << evictedReq->getId()
                << " for new PREMIUM request " << req->getId() << "\n";

            requests_.push_back(req);
            req->setBufferEnterTime(req->getArrivalTime());
            return true;
        }
        else {
            // All are premium or corporate
            controller_->incrementRejectedRequests();
            controller_->incrementRejectedByPriority(newPr);
            return false;
        }
    }

    if (newPr == Priority::CORPORATE) {
        // First try to remove FREE
        auto itFree = std::find_if(requests_.begin(), requests_.end(),
            [](auto& r) { return r->getPriority() == Priority::FREE; });
        if (itFree != requests_.end()) {
            auto evictedReq = *itFree;
            requests_.erase(itFree);
            controller_->incrementRejectedRequests();
            controller_->incrementRejectedByPriority(evictedReq->getPriority());

            std::cout << "Evicting FREE request " << evictedReq->getId()
                << " for new CORPORATE request " << req->getId() << "\n";

            requests_.push_back(req);
            req->setBufferEnterTime(req->getArrivalTime());
            return true;
        }
        else {
            // If no FREE, try to remove PREMIUM
            auto itPrem = std::find_if(requests_.begin(), requests_.end(),
                [](auto& r) { return r->getPriority() == Priority::PREMIUM; });
            if (itPrem != requests_.end()) {
                auto evictedReq = *itPrem;
                requests_.erase(itPrem);
                controller_->incrementRejectedRequests();
                controller_->incrementRejectedByPriority(evictedReq->getPriority());

                std::cout << "Evicting PREMIUM request " << evictedReq->getId()
                    << " for new CORPORATE request " << req->getId() << "\n";

                requests_.push_back(req);
                req->setBufferEnterTime(req->getArrivalTime());
                return true;
            }
            else {
                // All are corporate
                controller_->incrementRejectedRequests();
                controller_->incrementRejectedByPriority(newPr);
                return false;
            }
        }
    }

    // Otherwise, we reject
    controller_->incrementRejectedRequests();
    controller_->incrementRejectedByPriority(newPr);
    return false;
}

// Pop a request from the front of the buffer
std::shared_ptr<Request> Buffer::popRequest() {
    if (!requests_.empty()) {
        auto req = requests_.front();
        requests_.pop_front();
        return req;
    }
    return nullptr;
}

// Check if the buffer is empty
bool Buffer::isEmpty() const {
    return requests_.empty();
}

//------------------------------------------------------------------------------
// Device class
//------------------------------------------------------------------------------
Device::Device(int id)
    : id_(id),
    busy_(false),
    finishTime_(0.0),
    busyTotalTime_(0.0),
    startBusyTime_(0.0),
    serviceTimeHours_(0.0)
{
    rng_.seed(std::random_device{}());
}

// Check if the device is busy
bool Device::isBusy() const {
    return busy_;
}

// Get the finish time of the current request
double Device::getFinishTime() const {
    return finishTime_;
}

// Get the total busy time of the device
double Device::getBusyTotalTime() const {
    return busyTotalTime_;
}

// Load a request onto the device and generate service time
void Device::loadRequest(const std::shared_ptr<Request>& req, double currentTimeHours) {
    busy_ = true;
    currentRequest_ = req;
    startBusyTime_ = currentTimeHours;

    // Generate an exponential service time
    std::exponential_distribution<double> serviceDist(SERVICE_RATE);
    serviceTimeHours_ = serviceDist(rng_);
    finishTime_ = currentTimeHours + serviceTimeHours_;

    req->setStartServiceTime(currentTimeHours);

    int serviceTimeMinutes = static_cast<int>(std::round(serviceTimeHours_ * 60.0));
    std::cout << "Device " << id_
        << ": request " << req->getId()
        << " started at " << formatTime(currentTimeHours)
        << ", estimated finish " << formatTime(finishTime_)
        << " (service " << serviceTimeMinutes << " min)\n";
}

// Free the device after completing a request
void Device::freeDevice(double timeHours) {
    if (currentRequest_) {
        std::cout << "Device " << id_
            << ": request " << currentRequest_->getId()
            << " finished at " << formatTime(timeHours) << "\n";
    }
    busyTotalTime_ += (timeHours - startBusyTime_);
    busy_ = false;
    currentRequest_.reset();
}

// Get the generated service time in hours
double Device::getServiceTimeHours() const {
    return serviceTimeHours_;
}

int Device::getId() const {
    return id_;
}

//------------------------------------------------------------------------------
// Source class
//------------------------------------------------------------------------------
Source::Source(Priority priority, int sourceIndex)
    : priority_(priority),
    sourceIndex_(sourceIndex)
{
    rng_.seed(std::random_device{}());
}

// Generate the inter-arrival time for the next request
double Source::generateInterArrivalTime(double currentTimeHours) {
    double lambda = getArrivalRate(currentTimeHours);
    std::exponential_distribution<double> dist(lambda);
    return dist(rng_);
}

// Create a new request
std::shared_ptr<Request> Source::createRequest(int requestId, double arrivalTimeHours) {
    return std::make_shared<Request>(
        requestId,
        priority_,
        arrivalTimeHours,
        sourceIndex_
    );
}

// Schedule the next request generation
void Source::scheduleNextRequest(Controller& controller, double currentTime) {
    // If we already generated maxRequests, do not create more
    if (controller.getGeneratedRequestsCountRef() >= controller.getMaxRequests()) {
        return;
    }

    double delta = generateInterArrivalTime(currentTime);
    double arrivalTime = currentTime + delta;

    auto& globalId = controller.getGlobalRequestIdRef();
    globalId++;
    int newReqId = globalId;

    auto newReq = createRequest(newReqId, arrivalTime);
    controller.getGeneratedRequestsCountRef()++;

    // Push event: request generated
    controller.pushEvent(Event{
        EventType::REQUEST_GENERATED,
        arrivalTime,
        newReq,
        -1
        });
}

Priority Source::getPriority() const {
    return priority_;
}

int Source::getSourceIndex() const {
    return sourceIndex_;
}

//------------------------------------------------------------------------------
// Controller class
//------------------------------------------------------------------------------
Controller::Controller(int numCorporate, int numPremium, int numFree,
    int numDevices, int maxRequests)
    : buffer_(this),
    globalRequestId_(0),
    maxRequests_(maxRequests),
    generatedRequestsCount_(0),
    rejectedRequests_(0),
    totalWaitTime_(0.0),
    servedRequestsCount_(0),
    lastEventTime_(0.0)
{
    // Initialize rejections
    rejectedByPriority_[Priority::CORPORATE] = 0;
    rejectedByPriority_[Priority::PREMIUM] = 0;
    rejectedByPriority_[Priority::FREE] = 0;

    // Create source objects
    int sourceIndex = 0;
    for (int i = 0; i < numCorporate; ++i) {
        sources_.push_back(std::make_unique<Source>(Priority::CORPORATE, sourceIndex++));
    }
    for (int i = 0; i < numPremium; ++i) {
        sources_.push_back(std::make_unique<Source>(Priority::PREMIUM, sourceIndex++));
    }
    for (int i = 0; i < numFree; ++i) {
        sources_.push_back(std::make_unique<Source>(Priority::FREE, sourceIndex++));
    }

    // Create device objects
    for (int i = 1; i <= numDevices; ++i) {
        devices_.push_back(std::make_unique<Device>(i));
    }

    rng_.seed(std::random_device{}());
}

void Controller::initRequests() {
    // Initialize first requests for each source at time = 0
    double startTime = 0.0;
    for (auto& src : sources_) {
        if (generatedRequestsCount_ >= maxRequests_) {
            break;
        }
        double interArrival = src->generateInterArrivalTime(startTime);
        double arrivalTime = startTime + interArrival;

        globalRequestId_++;
        auto newReq = src->createRequest(globalRequestId_, arrivalTime);
        generatedRequestsCount_++;

        events_.push(Event{
            EventType::REQUEST_GENERATED,
            arrivalTime,
            newReq,
            -1
            });
    }
}

void Controller::work() {
    // Continue until we serve at least maxRequests_ requests
    while (servedRequestsCount_ < maxRequests_) {
        if (events_.empty()) {
            // No more events => stop
            std::cout << "No more events, simulation ends.\n";
            break;
        }

        Event currentEvent = popEvent();
        double currentTime = currentEvent.time;
        updateLastEventTime(currentTime);

        if (currentEvent.type == EventType::REQUEST_GENERATED) {
            handleRequestGenerated(currentEvent.request, currentTime);
        }
        else if (currentEvent.type == EventType::REQUEST_SERVED) {
            handleRequestFinished(currentEvent.deviceId, currentTime, currentEvent.request);
        }
    }
}

// Handle a newly generated request
void Controller::handleRequestGenerated(const std::shared_ptr<Request>& req, double currentTime) {
    std::cout << "Request " << req->getId()
        << " generated at " << formatTime(currentTime)
        << " with priority " << static_cast<int>(req->getPriority())
        << ".\n";

    bool added = buffer_.addRequest(req);
    if (!added) {
        std::cout << "Request " << req->getId() << " rejected.\n";
    }
    else {
        // Attempt to load into devices immediately if any are free
        loadRequestsToFreeDevices(currentTime);
    }

    // Schedule the next request from the same source
    int srcIdx = req->getSourceIndex();
    if (srcIdx >= 0 && srcIdx < static_cast<int>(getSources().size())) {
        getSources()[srcIdx]->scheduleNextRequest(*this, currentTime);
    }
}

// Handle the completion of a request
void Controller::handleRequestFinished(int deviceId, double currentTime, const std::shared_ptr<Request>& req) {
    // The device frees itself
    devices_[deviceId - 1]->freeDevice(currentTime);
    // Load next request from the buffer
    loadRequestsToFreeDevices(currentTime);
}

void Controller::printStatistics() {
    std::cout << "\n--- Final statistics ---\n";
    std::cout << "Total requests generated:  " << generatedRequestsCount_ << "\n";
    std::cout << "Total requests served:     " << servedRequestsCount_ << "\n";
    std::cout << "Total rejected requests:   " << rejectedRequests_ << "\n";

    std::cout << "Rejected Corporate: " << rejectedByPriority_[Priority::CORPORATE] << "\n";
    std::cout << "Rejected Premium:   " << rejectedByPriority_[Priority::PREMIUM] << "\n";
    std::cout << "Rejected Free:      " << rejectedByPriority_[Priority::FREE] << "\n";

    double avgWaitTime = 0.0;
    if (servedRequestsCount_ > 0) {
        avgWaitTime = totalWaitTime_ / (double)servedRequestsCount_;
    }
    std::cout << "Average waiting time (hours): " << avgWaitTime
        << " (~" << (avgWaitTime * 60.0) << " min)\n";

    std::cout << "\nDevices utilization:\n";
    for (auto& dev : devices_) {
        double busyTime = dev->getBusyTotalTime();
        double utilization = (lastEventTime_ > 0.0)
            ? (busyTime / lastEventTime_)
            : 0.0;

        std::cout << "  Device " << dev->getId()
            << ": busy " << busyTime << " h, load "
            << (utilization * 100.0) << " %\n";
    }

    std::cout << "\nTotal simulation time: " << lastEventTime_ << " hours\n";
}

void Controller::incrementRejectedRequests() {
    rejectedRequests_++;
}

void Controller::incrementRejectedByPriority(Priority pr) {
    rejectedByPriority_[pr]++;
}

int Controller::getRejectedByPriority(Priority p) const {
    auto it = rejectedByPriority_.find(p);
    if (it != rejectedByPriority_.end()) {
        return it->second;
    }
    return 0;
}

int Controller::getServedRequestsCount() const {
    return servedRequestsCount_;
}

void Controller::pushEvent(const Event& ev) {
    events_.push(ev);
}

bool Controller::eventsEmpty() const {
    return events_.empty();
}

Event Controller::popEvent() {
    auto topEvent = events_.top();
    events_.pop();
    return topEvent;
} // TODO: 

int& Controller::getGlobalRequestIdRef() {
    return globalRequestId_;
}

int Controller::getGlobalRequestId() const {
    return globalRequestId_;
}

int& Controller::getGeneratedRequestsCountRef() {
    return generatedRequestsCount_;
}

int Controller::getMaxRequests() const {
    return maxRequests_;
}

std::vector<std::unique_ptr<Device>>& Controller::getDevices() {
    return devices_;
}

std::vector<std::unique_ptr<Source>>& Controller::getSources() {
    return sources_;
}

void Controller::loadRequestsToFreeDevices(double currentTime) {
    for (auto& device : devices_) {
        if (!device->isBusy()) {
            auto nextReq = buffer_.popRequest();
            if (nextReq) {
                device->loadRequest(nextReq, currentTime);

                double waitTime = currentTime - nextReq->getBufferEnterTime();
                totalWaitTime_ += waitTime;
                servedRequestsCount_++;

                double serviceDuration = device->getServiceTimeHours();
                pushEvent(Event{
                    EventType::REQUEST_SERVED,
                    currentTime + serviceDuration,
                    nextReq,
                    device->getId()
                    });
            }
            else {
                break;
            }
        }
    }
}

void Controller::updateLastEventTime(double t) {
    if (t > lastEventTime_) {
        lastEventTime_ = t;
    }
}