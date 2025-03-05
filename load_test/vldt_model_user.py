from vldt import DataModel
from typing import Optional, List, Dict
from datetime import datetime


class Address(DataModel):
    street: str
    city: str
    postal_code: str


class Company(DataModel):
    name: str
    industry: str
    employees: int


class Profile(DataModel):
    username: str
    email: str
    bio: Optional[str]
    website: Optional[str]


class BankAccount(DataModel):
    account_number: str
    balance: float
    transactions: List[Dict[str, float]]


class Preferences(DataModel):
    theme: str
    language: str
    notifications_enabled: bool


class UserModel(DataModel):
    id: int
    name: str
    age: int
    is_active: bool
    registered_at: datetime
    address: Address
    company: Company
    profile: Profile
    bank_account: BankAccount
    preferences: Preferences
    scores: List[int]
    attributes: Dict[str, str]
    security_level: int
    friends: List[str]
    metadata: Dict[str, Dict[str, str]]
    tags: List[str]
    rating: float
    phone_number: str
    additional_info: Dict[str, str]
    bonus: float
    score_multiplier: float
    level: int
