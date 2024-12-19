#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <vector>
#include <random>
#include <sys/wait.h>
#include <mutex>

constexpr int STORE_CAPACITY = 8;
constexpr int NAME_MAX_LENGTH = 50;

// Product class with static ID generator
class Product
{
private:
    static int next_id; // Static ID generator
    int id;
    char name[NAME_MAX_LENGTH];
    int price;

public:
    Product() : id(0), price(0)
    {
        memset(name, 0, NAME_MAX_LENGTH);
    }

    Product(const char *name, int price) : id(next_id++), price(price)
    {
        strncpy(this->name, name, NAME_MAX_LENGTH - 1);
        this->name[NAME_MAX_LENGTH - 1] = '\0';
    }

    int get_id() const { return id; }
    const char *get_name() const { return name; }
    int get_price() const { return price; }

    std::string toString() const
    {
        return "ID: " + std::to_string(id) + "\nPrice: " + std::to_string(price) + "\nName: " + name;
    }
};

int Product::next_id = 1; // Initialize static ID generator

// Process-safe circular buffer store
class Store
{
private:
    Product products[STORE_CAPACITY];
    int front, rear, size;
    sem_t *sem_empty;
    sem_t *sem_full;
    sem_t *sem_mutex;

public:
    Store(sem_t *sem_empty, sem_t *sem_full, sem_t *sem_mutex)
        : front(0), rear(0), size(0), sem_empty(sem_empty), sem_full(sem_full), sem_mutex(sem_mutex) {}

    void store_product(const Product &product)
    {
        sem_wait(sem_empty); // Wait until there is space in the buffer
        sem_wait(sem_mutex); // Wait for exclusive access to the buffer

        products[rear] = product;
        rear = (rear + 1) % STORE_CAPACITY;
        size++;

        sem_post(sem_mutex); // Release exclusive access
        sem_post(sem_full);
    }

    Product restore_product()
    {
        sem_wait(sem_full);  // Wait until a product is available
        sem_wait(sem_mutex); // Wait for exclusive access to the buffer

        Product product = products[front];
        front = (front + 1) % STORE_CAPACITY;
        size--;

        sem_post(sem_mutex); // Release exclusive access
        sem_post(sem_empty); // Indicate that space is available

        return product;
    }
};

std::vector<std::pair<std::string, int>> product_catalog = {
    {"iPhone 14 Pro Max", 14000},
    {"Samsung Galaxy S23 5G", 12000},
    {"Apple Watch S9 45mm GPS+CEL", 7000},
    {"Samsung Galaxy Watch5 Pro 45mm LTE", 6000},
};

int generate_random(int lower, int upper)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(lower, upper);
    return dis(gen);
}

void producer(Store &store)
{
    while (true)
    {
        int product_index = generate_random(0, product_catalog.size() - 1);
        const auto &product_data = product_catalog[product_index];
        Product new_product(product_data.first.c_str(), product_data.second);

        store.store_product(new_product);
        sleep(generate_random(1, 3));
    }
}

void consumer(int consumer_id, Store &store)
{
    while (true)
    {
        Product consumed_product = store.restore_product();

        std::cout << "============= Customer " << consumer_id << " =============\n";
        std::cout << consumed_product.toString() << "\n";

        sleep(generate_random(1, 5));
    }
}

int main()
{
    void *shared_mem = mmap(nullptr, sizeof(sem_t) * 3 + sizeof(Store), PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_mem == MAP_FAILED)
    {
        std::cerr << "Failed to allocate shared memory.\n";
        return EXIT_FAILURE;
    }

    // Shared memory setup
    sem_t *sem_empty = static_cast<sem_t *>(shared_mem);
    sem_t *sem_full = sem_empty + 1;
    sem_t *sem_mutex = sem_full + 1;
    Store *store = reinterpret_cast<Store *>(sem_mutex + 1);

    // Initialize semaphores
    sem_init(sem_empty, 1, STORE_CAPACITY);
    sem_init(sem_full, 1, 0);
    sem_init(sem_mutex, 1, 1);

    // Construct Store in shared memory
    new (store) Store(sem_empty, sem_full, sem_mutex);

    pid_t consumers[4];
    for (int i = 0; i < 4; ++i)
    {
        sleep(generate_random(1, 2));
        pid_t pid = fork();
        if (pid == 0)
        {
            consumer(i + 1, *store);
            return 0;
        }
        consumers[i] = pid;
    }

    producer(*store);

    for (pid_t pid : consumers)
    {
        waitpid(pid, nullptr, 0);
    }

    // Cleanup
    sem_destroy(sem_empty);
    sem_destroy(sem_full);
    sem_destroy(sem_mutex);
    munmap(shared_mem, sizeof(sem_t) * 3 + sizeof(Store));

    return 0;
}
