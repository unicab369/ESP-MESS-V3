class PriorityRequestScheduler {
    constructor(maxConcurrent = 3) {
        this.queue = [];
        this.active = 0;
        this.maxConcurrent = maxConcurrent;
    }

    async add(requestFn, priority = 0) {
        return new Promise((resolve, reject) => {
            this.queue.push({ priority, requestFn, resolve, reject });
            this.queue.sort((a, b) => b.priority - a.priority);
            this.process();
        });
    }

    async process() {
        if (this.active >= this.maxConcurrent || this.queue.length === 0) return;

        this.active++;
        const { requestFn, resolve, reject } = this.queue.shift();

        try {
            const result = await requestFn();
            resolve(result); // ✅ Response handled here
        } catch (error) {
            reject(error);   // ✅ Error handled here
        } finally {
            this.active--;
            this.process();
        }
    }
}

const scheduler = new PriorityRequestScheduler(5);