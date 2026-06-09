'use server'

import { createAdminClient } from '@/lib/supabase-server'
import { revalidatePath } from 'next/cache'

export type CANSignalRow = {
  id: string
  vehicle_id: string
  frame_id: number
  direction: 'rx' | 'tx'
  tx_interval_ms: number
  dual_mode: boolean
  signal_name: string
  byte_start: number
  byte_length: number
  bit_mask: number
  big_endian: boolean
  is_signed: boolean
  scale: number
  offset_val: number
}

export async function getSignals(vehicleId: string): Promise<CANSignalRow[]> {
  const supabase = createAdminClient()
  const { data, error } = await supabase
    .from('can_signals')
    .select('*')
    .eq('vehicle_id', vehicleId)
    .order('frame_id', { ascending: true })
    .order('byte_start', { ascending: true })
  if (error) throw new Error(error.message)
  return (data ?? []) as CANSignalRow[]
}

export async function addSignal(signal: Omit<CANSignalRow, 'id'>): Promise<void> {
  const supabase = createAdminClient()
  const { error } = await supabase.from('can_signals').insert([signal])
  if (error) throw new Error(error.message)
  revalidatePath('/signals')
}

export async function updateSignal(id: string, signal: Omit<CANSignalRow, 'id'>): Promise<void> {
  const supabase = createAdminClient()
  const { error } = await supabase.from('can_signals').update(signal).eq('id', id)
  if (error) throw new Error(error.message)
  revalidatePath('/signals')
}

export async function deleteSignal(id: string): Promise<void> {
  const supabase = createAdminClient()
  const { error } = await supabase.from('can_signals').delete().eq('id', id)
  if (error) throw new Error(error.message)
  revalidatePath('/signals')
}
