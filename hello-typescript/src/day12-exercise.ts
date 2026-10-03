interface Product {
    id: number;
    name: string;
    price: number;
    description: string;
    stock: number;
}

// create product, no id
type CreateProductInput = Omit<Product, "id">;

// update, no id, other optional
type UpdateProductInput = Partial<Omit<Product, "id">>;

// show: id, name, price
type ProductSummary = Pick<Product, "id" | "name" | "price">;

type Category = "electronics" | "clothing" | "books";

const productCategories: Record<Category, Product[]> = {
    electronics: [],
    clothing: [],
    books: [],
};

const products: Product[] = [];
let nextProductId = 1;

function createProduct(input: CreateProductInput, category: Category): Product {
    const product: Product = { id: nextProductId++, ...input };
    products.push(product);
    productCategories[category].push(product);
    return product;
}

function updateProduct(id: number, input: UpdateProductInput): Product | undefined {
    const product = products.find(p => p.id === id);
    if (product) {
        Object.assign(product, input);
        return product;
    }
    return undefined;
}

function toCard(product: Product): ProductSummary {
    const { id, name, price } = product;
    return { id, name, price };
}

// Example usage
const newProduct = createProduct({ name: "Laptop", price: 999.99, description: "A high-end laptop", stock: 10 }, "electronics");
console.log(toCard(newProduct)); // Output: { id: 1, name: "Laptop", price: 999.99 }

const newProduct2 = createProduct({ name: "T-Shirt", price: 19.99, description: "A comfortable t-shirt", stock: 50 }, "clothing");
console.log(toCard(newProduct2)); // Output: { id: 2, name: "T-Shirt", price: 19.99 }

const updatedProduct = updateProduct(1, { price: 899.99, stock: 8 });
if (updatedProduct) {
    console.log(toCard(updatedProduct)); // Output: { id: 1, name: "Laptop", price: 899.99 }
}

console.log(productCategories); // Output: { electronics: [ { id: 1, name: "Laptop", price: 899.99, description: "A high-end laptop", stock: 8 } ], clothing: [ { id: 2, name: "T-Shirt", price: 19.99, description: "A comfortable t-shirt", stock: 50 } ], books: [] }
console.log(products.map(toCard)); // Output: [ { id: 1, name: "Laptop", price: 899.99 }, { id: 2, name: "T-Shirt", price: 19.99 } ]

for (const category of Object.keys(productCategories) as Category[]) {
    console.log(category, productCategories[category].map(toCard));
}