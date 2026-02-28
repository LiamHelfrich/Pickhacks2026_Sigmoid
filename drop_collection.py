from pymongo import MongoClient

# Connect to MongoDB
client = MongoClient("mongodb://localhost:27017/")  # Change URI as needed

db = client["maindb"]
collection = db["data"]  # Change to your collection name

collection.drop()
print(f"Collection '{collection.name}' dropped.")

client.close()