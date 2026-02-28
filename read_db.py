from pymongo import MongoClient

# Connect to MongoDB
client = MongoClient("mongodb://localhost:27017/")  # Change URI as needed

db = client["maindb"]

# List all collections
collections = db.list_collection_names()
print(f"Collections in 'maindb': {collections}\n")

# List all documents in each collection
for collection_name in collections:
    collection = db[collection_name]
    documents = list(collection.find())
    print(f"--- {collection_name} ({len(documents)} documents) ---")
    for doc in documents:
        print(doc)
    print()

client.close()