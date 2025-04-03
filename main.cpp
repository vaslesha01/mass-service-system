    #include <iostream>
    #include "simulation.hpp"

    int main() {
        // 2 corp, 4 prem, 6 free,
        // 5 devices, 5000 requests
        Controller controller(2, 4, 6, 5, 5000);

        controller.initRequests();

        controller.work();

        controller.printStatistics();

        return 0;
    }