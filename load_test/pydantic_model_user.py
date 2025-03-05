from pydantic import BaseModel
from typing import Optional, List, Dict
from datetime import datetime


class Address(BaseModel):
    street: str
    city: str
    postal_code: str


class Company(BaseModel):
    name: str
    industry: str
    employees: int


class Profile(BaseModel):
    username: str
    email: str
    bio: Optional[str] = None
    website: Optional[str] = None


class BankAccount(BaseModel):
    account_number: str
    balance: float
    transactions: List[Dict[str, float]]


class Preferences(BaseModel):
    theme: str
    language: str
    notifications_enabled: bool


class UserModel(BaseModel):
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
